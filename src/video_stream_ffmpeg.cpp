#include "video_stream_ffmpeg.h"

#include <cmath>

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#define IO_BUFFER_SIZE 512 * 1024 // File reading buffer of 512 KiB
#define AUDIO_BUFFER_MAX_SIZE 192000
#define AUX_BUFFER_SIZE 1024
// TODO: is this sample rate defined somewhere in the godot api etc?
#define AUDIO_MIX_RATE 22050

using namespace godot;

FFMPEGPacketQueue::FFMPEGPacketQueue() {
	first_pkt = nullptr;
	last_pkt = nullptr;
	nb_packets = 0;
	size = 0;
}

FFMPEGPacketQueue::~FFMPEGPacketQueue() {
	AVPacket pt;
	while (get(&pt)) {
		av_packet_unref(&pt);
	}
}

void FFMPEGPacketQueue::flush() {
	AVPacketList *pkt, *pkt1;

	for (pkt = first_pkt; pkt; pkt = pkt1) {
		pkt1 = pkt->next;
		av_packet_unref(&pkt->pkt);
		memfree(pkt);
	}
	last_pkt = nullptr;
	first_pkt = nullptr;
	nb_packets = 0;
	size = 0;
}

int FFMPEGPacketQueue::put(AVPacket *pkt) {
	AVPacketList *pkt1;
	pkt1 = (AVPacketList *)memalloc(sizeof(AVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;

	if (!last_pkt)
		first_pkt = pkt1;
	else
		last_pkt->next = pkt1;
	last_pkt = pkt1;
	nb_packets++;
	size += pkt1->pkt.size;
	return 0;
}

int FFMPEGPacketQueue::get(AVPacket *pkt) {
	AVPacketList *pkt1 = first_pkt;
	if (!pkt1) {
		return 0;
	}

	first_pkt = pkt1->next;
	if (!first_pkt)
		last_pkt = NULL;
	nb_packets--;
	size -= pkt1->pkt.size;
	*pkt = pkt1->pkt;
	memfree(pkt1);
	return 1;
}


void VideoStreamPlaybackFFMPEG::_bind_methods() {
}

void VideoStreamPlaybackFFMPEG::_play() {
	_stop();

	delay_compensation = ProjectSettings::get_singleton()->get("audio/video/video_delay_compensation_ms");
	delay_compensation /= 1000.0;

	playing = true;
}

void VideoStreamPlaybackFFMPEG::_stop() {
	if (playing) {
		_seek(0);
	}
	playing = false;
}

bool VideoStreamPlaybackFFMPEG::_is_playing() const {
	return playing;
}

void VideoStreamPlaybackFFMPEG::_set_paused(bool p_paused) {
	paused = p_paused;
}

bool VideoStreamPlaybackFFMPEG::_is_paused() const {
	return paused;
}

double VideoStreamPlaybackFFMPEG::_get_length() const {
	if(!data.format_ctx) {
		WARN_PRINT_ONCE("Format context is null.");
		return -1;
	}

	return data.format_ctx->streams[data.videostream_idx]->duration * av_q2d(data.format_ctx->streams[data.videostream_idx]->time_base);
}

double VideoStreamPlaybackFFMPEG::_get_playback_position() const {
	if (data.format_ctx) {
		bool use_v_pts = data.frame_yuv->pts != AV_NOPTS_VALUE && data.position_type == POS_V_PTS;
		bool use_a_time = data.position_type == POS_A_TIME;
		bool in_update = data.position_type == POS_V_PTS;
		// TODO: The fuck this is doing?
		//data.position_type = POS_TIME;

		if (use_v_pts) {
			double pts = (double)data.frame_yuv->pts;
			pts *= av_q2d(data.format_ctx->streams[data.videostream_idx]->time_base);
			return pts;
		} else {
			if (!isnan(data.audio_time) && use_a_time) {
				return data.audio_time;
			}
			// fudge the time if we in the first frame after an update but don't have V_PTS yet
			double adjustment = in_update ? -0.01 : 0.0;
			return data.time + adjustment;
		}
	}

	return 0;
}

void VideoStreamPlaybackFFMPEG::_seek(double p_time) {
	if(!data.loaded) {
		return;
	}

	// Hack to find the end of the video. Really VideoPlayer should expose this!
	if (p_time < 0) {
		p_time = data.format_ctx->duration / (double)AV_TIME_BASE;
	}
	int64_t seek_target = p_time * AV_TIME_BASE;
	// seek within 10 seconds of the selected spot.
	int64_t margin = 10 * AV_TIME_BASE;

	// printf("seek(): %fs = %lld\n", p_time, seek_target);
	int ret = avformat_seek_file(data.format_ctx, -1, seek_target - margin, seek_target, seek_target, 0);
	if (ret < 0) {
		WARN_PRINT("avformat_seek_file() can't seek backward?");
		ret = avformat_seek_file(data.format_ctx, -1, seek_target - margin, seek_target, seek_target + margin, 0);
	}
	if (ret < 0) {
		ERR_PRINT("avformat_seek_file() failed");
	} else {
		data.video_packet_queue->flush();
		data.audio_packet_queue->flush();
		flush_frames(data.vcodec_ctx);
		avcodec_flush_buffers(data.vcodec_ctx);
		if (data.acodec_ctx) {
			flush_frames(data.acodec_ctx);
			avcodec_flush_buffers(data.acodec_ctx);
		}
		data.num_decoded_samples = 0;
		data.audio_buffer_pos = 0;
		data.time = p_time;
		data.seek_time = p_time;
		// try to use the audio time as the seek position
		data.position_type = POS_A_TIME;
		data.audio_time = NAN;
	}

	if (p_time < time) {
		seek_backward = true;
	}
	time = p_time;
	// reset audio buffers
	pcm.fill(0);
	pcm_write_idx = -1;
	samples_decoded = 0;
}

void VideoStreamPlaybackFFMPEG::_set_file(const String &p_file) {
	// Clean up the previous file.
	_cleanup();

	data.io_buffer = (uint8_t *)memalloc(IO_BUFFER_SIZE * sizeof(uint8_t));
	if (!data.io_buffer) {
		_cleanup();
		WARN_PRINT("Buffer alloc error");
		return;
	}

	file = FileAccess::open(p_file, FileAccess::READ);
	if (!file.is_valid()) {
		_cleanup();
		ERR_PRINT("File not found.");
		return;
	}

	int read_bytes = raw_file_read(file.ptr(), data.io_buffer, IO_BUFFER_SIZE);

	// Rewind to 0
	file->seek(0);

	// Determine input format
	AVProbeData probe_data;
	probe_data.buf = data.io_buffer;
	probe_data.buf_size = read_bytes;
	probe_data.filename = "";
	probe_data.mime_type = "";

	AVInputFormat *input_format = av_probe_input_format(&probe_data, 1);
	if (input_format == NULL) {
		_cleanup();
		ERR_PRINT(vformat("Format not recognized: %s (%s)", probe_data.filename, probe_data.mime_type));
		return;
	}
	input_format->flags |= AVFMT_SEEK_TO_PTS;

	data.io_ctx = avio_alloc_context(data.io_buffer, IO_BUFFER_SIZE, 0, file.ptr(),
			raw_file_read, NULL, raw_file_seek);
	if (data.io_ctx == NULL) {
		_cleanup();
		ERR_PRINT("IO context alloc error.");
		return;
	}

	data.format_ctx = avformat_alloc_context();
	if (data.format_ctx == NULL) {
		_cleanup();
		ERR_PRINT("Format context alloc error.");
		return;
	}

	data.format_ctx->pb = data.io_ctx;
	data.format_ctx->flags = AVFMT_FLAG_CUSTOM_IO;
	data.format_ctx->iformat = input_format;

	if (avformat_open_input(&data.format_ctx, "", NULL, NULL) != 0) {
		_cleanup();
		ERR_PRINT("Input stream failed to open");
		return;
	}
	data.input_open = true;

	if (avformat_find_stream_info(data.format_ctx, NULL) < 0) {
		_cleanup();
		ERR_PRINT("Could not find stream info.");
		return;
	}

	data.videostream_idx = -1; // should be -1 anyway, just being paranoid.
	data.audiostream_idx = -1;
	// find stream
	for (int i = 0; i < data.format_ctx->nb_streams; i++) {
		if (data.format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			data.videostream_idx = i;
		} else if (data.format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			data.audiostream_idx = i;
		}
	}
	if (data.videostream_idx == -1) {
		_cleanup();
		ERR_PRINT("Video Stream not found.");
		return;
	}

	AVCodecParameters *vcodec_param = data.format_ctx->streams[data.videostream_idx]->codecpar;

	AVCodec *vcodec = NULL;
	vcodec = avcodec_find_decoder(vcodec_param->codec_id);
	if (vcodec == NULL) {
		const AVCodecDescriptor *desc = avcodec_descriptor_get(vcodec_param->codec_id);
		WARN_PRINT(vformat("Videodecoder %s (%s) not found.", desc->name, desc->long_name));
		_cleanup();
		return;
	}

	data.vcodec_ctx = avcodec_alloc_context3(vcodec);
	if (data.vcodec_ctx == NULL) {
		_cleanup();
		WARN_PRINT("Videocodec allocation error.");
		return;
	}

	if (avcodec_parameters_to_context(data.vcodec_ctx, vcodec_param) < 0) {
		_cleanup();
		WARN_PRINT("Videocodec context init error.");
		return;
	}
	// enable multi-thread decoding based on CPU core count
	data.vcodec_ctx->thread_count = 0;

	if (avcodec_open2(data.vcodec_ctx, vcodec, NULL) < 0) {
		_cleanup();
		WARN_PRINT("Videocodec failed to open.");
		return;
	}
	data.vcodec_open = true;

	AVCodecParameters *acodec_param = NULL;
	AVCodec *acodec = NULL;
	if (data.audiostream_idx >= 0) {
		acodec_param = data.format_ctx->streams[data.audiostream_idx]->codecpar;

		acodec = avcodec_find_decoder(acodec_param->codec_id);
		if (acodec == NULL) {
			const AVCodecDescriptor *desc = avcodec_descriptor_get(acodec_param->codec_id);
			WARN_PRINT(vformat("Audiodecoder %s (%s) not found.", desc->name, desc->long_name));
			_cleanup();
			return;
		}
		data.acodec_ctx = avcodec_alloc_context3(acodec);
		if (data.acodec_ctx == NULL) {
			_cleanup();
			ERR_PRINT("Audiocodec allocation error.");
			return;
		}

		if (avcodec_parameters_to_context(data.acodec_ctx, acodec_param) < 0) {
			_cleanup();
			ERR_PRINT("Audiocodec context init error.");
			return;
		}

		if (avcodec_open2(data.acodec_ctx, acodec, NULL) < 0) {
			_cleanup();
			ERR_PRINT("Audiocodec failed to open.");
			return;
		}
		data.acodec_open = true;

		data.audio_buffer = (float *)memalloc(AUDIO_BUFFER_MAX_SIZE * sizeof(float));
		if (data.audio_buffer == NULL) {
			_cleanup();
			ERR_PRINT("Audio buffer alloc failed.");
			return;
		}

		data.audio_frame = av_frame_alloc();
		if (data.audio_frame == NULL) {
			_cleanup();
			ERR_PRINT("Frame alloc fail.");
			return;
		}

		data.swr_ctx = swr_alloc();
		av_opt_set_int(data.swr_ctx, "in_channel_layout", data.acodec_ctx->channel_layout, 0);
		av_opt_set_int(data.swr_ctx, "out_channel_layout", data.acodec_ctx->channel_layout, 0);
		av_opt_set_int(data.swr_ctx, "in_sample_rate", data.acodec_ctx->sample_rate, 0);
		av_opt_set_int(data.swr_ctx, "out_sample_rate", AUDIO_MIX_RATE, 0);
		av_opt_set_sample_fmt(data.swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
		av_opt_set_sample_fmt(data.swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
		swr_init(data.swr_ctx);
	}

	// NOTE: Align of 1 (I think it is for 32 bit alignment.) Doesn't work otherwise
	data.frame_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGB32,
			data.vcodec_ctx->width, data.vcodec_ctx->height, 1);

	data.frame_buffer = (uint8_t *)memalloc(data.frame_buffer_size);
	if (data.frame_buffer == NULL) {
		_cleanup();
		ERR_PRINT("Framebuffer alloc fail.");
		return;
	}

	data.frame_rgb = av_frame_alloc();
	if (data.frame_rgb == NULL) {
		_cleanup();
		ERR_PRINT("Frame alloc fail.");
		return;
	}

	data.frame_yuv = av_frame_alloc();
	if (data.frame_yuv == NULL) {
		_cleanup();
		ERR_PRINT("Frame alloc fail.");
		return;
	}

	int width = data.vcodec_ctx->width;
	int height = data.vcodec_ctx->height;
	if (av_image_fill_arrays(data.frame_rgb->data, data.frame_rgb->linesize, data.frame_buffer,
				AV_PIX_FMT_RGB32, width, height, 1) < 0) {
		_cleanup();
		ERR_PRINT("Frame fill.");
		return;
	}

	data.sws_ctx = sws_getContext(width, height, data.vcodec_ctx->pix_fmt,
			width, height, AV_PIX_FMT_RGB0, SWS_BILINEAR,
			NULL, NULL, NULL);
	if (data.sws_ctx == NULL) {
		_cleanup();
		ERR_PRINT("Swscale context not created.");
		return;
	}

	data.time = 0;
	data.num_decoded_samples = 0;

	data.audio_packet_queue = memnew(FFMPEGPacketQueue);
	data.video_packet_queue = memnew(FFMPEGPacketQueue);

	data.drop_frame = 0;
	data.total_frame = 0;

	data.loaded = true;

	// Only do memset if num_channels > 0 otherwise it will crash.
	int num_channels = _get_channels();
	if (num_channels > 0) {
		pcm.resize(num_channels * AUX_BUFFER_SIZE);
		pcm.fill(0);
	}

	pcm_write_idx = -1;

	Ref<Image> img = Image::create(data.vcodec_ctx->width, data.vcodec_ctx->height, false, Image::FORMAT_RGBA8);
	texture->set_image(img);
}

Ref<Texture2D> VideoStreamPlaybackFFMPEG::_get_texture() const {
	return texture;
}

void VideoStreamPlaybackFFMPEG::_update(double p_delta) {
	// during an 'update' make sure to use the video frame's pts timestamp
	// otherwise the godot VideoStreamNative update method
	// won't even try to request a frame since it expects the plugin
	// to use video presentation timestamp as the source of time.
	if (!playing || paused) {
		return;
	}
	if (!file.is_valid() || !data.loaded) {
		return;
	}
	time += p_delta;

	data.position_type = POS_V_PTS;

	data.time += p_delta;
	// afford one frame worth of slop when decoding
	data.diff_tolerance = p_delta;

	if (!isnan(data.audio_time)) {
		data.audio_time += p_delta;
	}
	read_frame();

	// Don 't mix if there' s no audio(num_channels == 0).if (mix_callback && num_channels > 0) {
	if (_get_channels() > 0) {
		if (pcm_write_idx >= 0) {
			// Previous remains
			int mixed = mix_audio(samples_decoded, pcm, pcm_write_idx * _get_channels());
			if (mixed == samples_decoded) {
				pcm_write_idx = -1;
			} else {
				samples_decoded -= mixed;
				pcm_write_idx += mixed;
			}
		}
		if (pcm_write_idx < 0) {
			samples_decoded = get_audio_frame(AUX_BUFFER_SIZE);
			pcm_write_idx = mix_audio(samples_decoded, pcm);
			if (pcm_write_idx == samples_decoded) {
				pcm_write_idx = -1;
			} else {
				samples_decoded -= pcm_write_idx;
			}
		}
	}

	if (seek_backward) {
		update_texture();
		seek_backward = false;
	}

	while (_get_playback_position() < time && playing) {
		update_texture();
	}
}

int VideoStreamPlaybackFFMPEG::_get_channels() const {
	if(data.acodec_ctx) {
		return data.acodec_ctx->channels;
	}
	return 0;
}

int VideoStreamPlaybackFFMPEG::_get_mix_rate() const {
	if(data.acodec_ctx) {
		return AUDIO_MIX_RATE;
	}
	return 0;
}

void VideoStreamPlaybackFFMPEG::_set_audio_track(int p_idx) {
}

int VideoStreamPlaybackFFMPEG::raw_file_read(void *ptr, uint8_t *buf, int buf_size) {
	// ptr is a FileAccess
	FileAccess *file = reinterpret_cast<FileAccess *>(ptr);

	// if file exists
	if (file) {
		int64_t bytes_read = file->get_buffer(buf, buf_size);
		return bytes_read;
	}
	return -1;
}

int64_t VideoStreamPlaybackFFMPEG::raw_file_seek(void *ptr, int64_t pos, int whence) {
	// file
	FileAccess *file = reinterpret_cast<FileAccess *>(ptr);

	if (file) {
		int64_t len = file->get_length();
		switch (whence) {
			case SEEK_SET: {
				if (pos > len) {
					return -1;
				}
				file->seek(pos);
				return file->get_position();
			} break;
			case SEEK_CUR: {
				// Just in case it doesn't exist
				if (pos < 0 && -pos > (int64_t)file->get_position()) {
					return -1;
				}
				file->seek(file->get_position() + pos);
				return file->get_position();
			} break;
			case SEEK_END: {
				// Just in case something goes wrong
				if (-pos > len) {
					return -1;
				}
				file->seek_end(pos);
				return file->get_position();
			} break;
			default: {
				// Only 4 possible options, hence default = AVSEEK_SIZE
				// Asks to return the length of file
				return len;
			} break;
		}
	}
	// In case nothing works out.
	return -1;
}


VideoStreamPlaybackFFMPEG::VideoStreamPlaybackFFMPEG() {
	data.io_buffer = nullptr;
	data.io_ctx = nullptr;

	data.format_ctx = nullptr;
	data.input_open = false;

	data.videostream_idx = -1;
	data.vcodec_ctx = nullptr;
	data.vcodec_open = false;

	data.frame_rgb = nullptr;
	data.frame_yuv = nullptr;
	data.sws_ctx = nullptr;

	data.frame_buffer = nullptr;
	data.frame_buffer_size = 0;

	data.audiostream_idx = -1;
	data.acodec_ctx = nullptr;
	data.acodec_open = false;
	data.audio_frame = nullptr;
	data.audio_buffer = nullptr;

	data.swr_ctx = nullptr;

	data.num_decoded_samples = 0;
	data.audio_buffer_pos = 0;

	data.audio_packet_queue = nullptr;
	data.video_packet_queue = nullptr;

	data.position_type = POS_A_TIME;
	data.time = 0;
	data.audio_time = NAN;

	data.frame_unwrapped = false;
	data.unwrapped_frame = PackedByteArray();

	data.loaded = false;

	texture = Ref<ImageTexture>(memnew(ImageTexture));
}

VideoStreamPlaybackFFMPEG::~VideoStreamPlaybackFFMPEG() {
	_cleanup();
};

void VideoStreamPlaybackFFMPEG::_cleanup() {
	if (data.audio_packet_queue) {
		memdelete(data.audio_packet_queue);
		data.audio_packet_queue = nullptr;
	}

	if (data.video_packet_queue) {
		memdelete(data.video_packet_queue);
		data.video_packet_queue = nullptr;
	}

	if (data.sws_ctx) {
		sws_freeContext(data.sws_ctx);
		data.sws_ctx = nullptr;
	}

	if (data.audio_frame) {
		av_frame_unref(data.audio_frame);
		data.audio_frame = nullptr;
	}

	if (data.frame_rgb) {
		av_frame_unref(data.frame_rgb);
		data.frame_rgb = nullptr;
	}

	if (data.frame_yuv) {
		av_frame_unref(data.frame_yuv);
		data.frame_yuv = nullptr;
	}

	if (data.frame_buffer) {
		memfree(data.frame_buffer);
		data.frame_buffer = nullptr;
		data.frame_buffer_size = 0;
	}

	if (data.vcodec_ctx) {
		if (data.vcodec_open) {
			avcodec_close(data.vcodec_ctx);
			data.vcodec_open = false;
		}
		avcodec_free_context(&data.vcodec_ctx);
		data.vcodec_ctx = nullptr;
	}

	if (data.acodec_ctx) {
		if (data.acodec_open) {
			avcodec_close(data.acodec_ctx);
			data.vcodec_open = false;
			avcodec_free_context(&data.acodec_ctx);
			data.acodec_ctx = nullptr;
		}
	}

	if (data.format_ctx) {
		if (data.input_open) {
			avformat_close_input(&data.format_ctx);
			data.input_open = false;
		}
		avformat_free_context(data.format_ctx);
		data.format_ctx = nullptr;
	}

	if (data.io_ctx) {
		avio_context_free(&data.io_ctx);
		data.io_ctx = nullptr;
	}

	if (data.io_buffer) {
		memfree(data.io_buffer);
		data.io_buffer = nullptr;
	}

	if (data.audio_buffer) {
		memfree(data.audio_buffer);
		data.audio_buffer = nullptr;
	}

	if (data.swr_ctx) {
		swr_free(&data.swr_ctx);
		data.swr_ctx = nullptr;
	}

	data.time = 0;
	data.seek_time = 0;
	data.diff_tolerance = 0;
	data.videostream_idx = -1;
	data.audiostream_idx = -1;
	data.num_decoded_samples = 0;
	data.audio_buffer_pos = 0;

	data.drop_frame = 0;
	data.total_frame = 0;

	data.loaded = false;
}

void VideoStreamPlaybackFFMPEG::flush_frames(AVCodecContext *ctx) {
	/**
	 * from https://www.ffmpeg.org/doxygen/4.1/group__lavc__encdec.html
	 * End of stream situations. These require "flushing" (aka draining) the codec, as the codec might buffer multiple frames or packets internally for performance or out of necessity (consider B-frames). This is handled as follows:
	 * Instead of valid input, send NULL to the avcodec_send_packet() (decoding) or avcodec_send_frame() (encoding) functions. This will enter draining mode.
	 * Call avcodec_receive_frame() (decoding) or avcodec_receive_packet() (encoding) in a loop until AVERROR_EOF is returned. The functions will not return AVERROR(EAGAIN), unless you forgot to enter draining mode.
	 * Before decoding can be resumed again, the codec has to be reset with avcodec_flush_buffers().
	 */
	int ret = avcodec_send_packet(ctx, NULL);
	AVFrame frame = { 0 };
	if (ret <= 0) {
		do {
			ret = avcodec_receive_frame(ctx, &frame);
		} while (ret != AVERROR_EOF);
	}
}

bool VideoStreamPlaybackFFMPEG::read_frame() {
	while (data.video_packet_queue->nb_packets < 24) {
		AVPacket pkt;
		int ret = av_read_frame(data.format_ctx, &pkt);
		if (ret >= 0) {
			if (pkt.stream_index == data.videostream_idx) {
				data.video_packet_queue->put(&pkt);
			} else if (pkt.stream_index == data.audiostream_idx) {
				data.audio_packet_queue->put(&pkt);
			} else {
				av_packet_unref(&pkt);
			}
		} else {
			return false;
		}
	}
	return true;
}

void VideoStreamPlaybackFFMPEG::update_texture() {
	PackedByteArray frame_data = get_video_frame();
	if (frame_data.size() == 0) {
		playing = false;
		return;
	}

	Ref<Image> img = Image::create_from_data(data.vcodec_ctx->width, data.vcodec_ctx->height, 0, Image::FORMAT_RGBA8, frame_data);
	texture->update(img);
}

PackedByteArray VideoStreamPlaybackFFMPEG::get_video_frame() {
	AVPacket pkt = { 0 };
	int ret;
	size_t drop_count = 0;
	// to maintain a decent game frame rate
	// don't let frame decoding take more than this number of ms
	uint64_t max_frame_drop_time = 5;
	// but we do need to drop frames, so try to drop at least some frames even if it's a bit slow :(
	size_t min_frame_drop_count = 5;
	uint64_t start = Time::get_singleton()->get_ticks_msec();

retry:
	ret = avcodec_receive_frame(data.vcodec_ctx, data.frame_yuv);
	if (ret == AVERROR(EAGAIN)) {
		// need to call avcodedc_send_packet, get a packet from queue to send it
		while (!data.video_packet_queue->get(&pkt)) {
			// api->godot_print_warning("video packet queue empty", "godot_videodecoder_get_videoframe()", __FILE__, __LINE__);
			if (!read_frame()) {
				return PackedByteArray();
			}
		}
		ret = avcodec_send_packet(data.vcodec_ctx, &pkt);
		if (ret < 0) {
			char err[512];
			av_strerror(ret, err, sizeof(err) - 1);
			ERR_PRINT(vformat("avcodec_send_packet returns %d (%s)", ret, err));
			av_packet_unref(&pkt);
			return PackedByteArray();
		}
		av_packet_unref(&pkt);
		goto retry;
	} else if (ret < 0) {
		ERR_PRINT(vformat("avcodec_receive_frame returns %d", ret));
		return PackedByteArray();
	}

	bool pts_correct = data.frame_yuv->pts == AV_NOPTS_VALUE;
	int64_t pts = pts_correct ? data.frame_yuv->pkt_dts : data.frame_yuv->pts;

	double ts = pts * av_q2d(data.format_ctx->streams[data.videostream_idx]->time_base);

	data.total_frame++;

	// frame successfully decoded here, now if it lags behind too much (diff_tolerance sec)
	// let's discard this frame and get the next frame instead
	bool drop = ts < data.time - data.diff_tolerance;
	uint64_t drop_duration = Time::get_singleton()->get_ticks_msec() - start;
	if (drop && drop_duration > max_frame_drop_time && drop_count < min_frame_drop_count && data.frame_unwrapped) {
		// only discard frames for max_frame_drop_time ms or we'll slow down the game's main thread!
		if (fabs(data.seek_time - data.time) > data.diff_tolerance * 10) {
			WARN_PRINT(
				vformat("Slow CPU? Dropped %d frames for %dms frame dropped: %d/%d (%.1f%%) pts=%.1f t=%.1f",
					(int)drop_count,
					(int)drop_duration,
					(int)data.drop_frame,
					(int)data.total_frame,
					100.0 * data.drop_frame / data.total_frame,
					ts, (double)data.time)
			);
		}
	} else if (drop) {
		drop_count++;
		data.drop_frame++;
		av_packet_unref(&pkt);
		goto retry;
	}
	if (!drop || fabs(data.seek_time - data.time) > data.diff_tolerance * 2) {
		// Don't overwrite the current frame when dropping frames for performance reasons
		// except when the time is within 2 frames of the most recent seek
		// because we don't want a glitchy 'fast forward' effect when seeking.
		// NOTE: VideoPlayer currently doesnt' ask for a frame when seeking while paused so you'd
		// have to fake it inside godot by unpausing briefly. (see FIG1 below)
		data.frame_unwrapped = true;
		sws_scale(data.sws_ctx, (uint8_t const *const *)data.frame_yuv->data, data.frame_yuv->linesize, 0,
				data.vcodec_ctx->height, data.frame_rgb->data, data.frame_rgb->linesize);
		int frame_size = data.vcodec_ctx->width * data.vcodec_ctx->height * 4;
		if (data.unwrapped_frame.size() != frame_size) {
			data.unwrapped_frame.resize(frame_size);
		}

		uint8_t *write_ptr = data.unwrapped_frame.ptrw();
		int val = 0;
		for (int y = 0; y < data.vcodec_ctx->height; y++) {
			memcpy(write_ptr, data.frame_rgb->data[0] + y * data.frame_rgb->linesize[0], data.vcodec_ctx->width * 4);
			write_ptr += data.vcodec_ctx->width * 4;
		}
	}
	av_packet_unref(&pkt);

	// hack to get video_stream_gdnative to stop asking for frames.
	// stop trusting video pts until the next time update() is called.
	// this will unblock VideoStreamPlaybackGDNative::update() which
	// keeps calling get_texture() until the time matches
	// we don't need this behavior as we already handle frame skipping internally.
	data.position_type = POS_TIME;
	return data.frame_unwrapped ? data.unwrapped_frame : PackedByteArray();
}

int VideoStreamPlaybackFFMPEG::get_audio_frame(int pcm_remaining) {
	if (data.audiostream_idx < 0) {
		return 0;
	}
	bool first_frame = true;

	// if playback has just started or just seeked then we enter the audio_reset state.
	// during audio_reset it's important to skip old samples
	// _and_ avoid sending samples from the future until the presentation timestamp syncs up.
	bool audio_reset = isnan(data.audio_time) || data.audio_time > data.time - data.diff_tolerance;

	const int pcm_buffer_size = pcm_remaining;
	int pcm_offset = 0;

	double p_time = data.audio_frame->pts * av_q2d(data.format_ctx->streams[data.audiostream_idx]->time_base);

	if (audio_reset && data.num_decoded_samples > 0) {
		// don't send any pcm data if the frame hasn't started yet
		if (p_time > data.time) {
			return 0;
		}
		// skip the any decoded samples if their presentation timestamp is too old
		if (data.time - p_time > data.diff_tolerance) {
			data.num_decoded_samples = 0;
		}
	}

	int sample_count = (pcm_remaining < data.num_decoded_samples) ? pcm_remaining : data.num_decoded_samples;

	if (sample_count > 0) {
		memcpy(pcm.ptrw(), data.audio_buffer + data.acodec_ctx->channels * data.audio_buffer_pos, sizeof(float) * sample_count * data.acodec_ctx->channels);
		pcm_offset += sample_count;
		pcm_remaining -= sample_count;
		data.num_decoded_samples -= sample_count;
		data.audio_buffer_pos += sample_count;
	}
	while (pcm_remaining > 0) {
		if (data.num_decoded_samples <= 0) {
			AVPacket pkt;

			int ret;
		retry_audio:
			ret = avcodec_receive_frame(data.acodec_ctx, data.audio_frame);
			if (ret == AVERROR(EAGAIN)) {
				// need to call avcodec_send_packet, get a packet from queue to send it
				if (!data.audio_packet_queue->get(&pkt)) {
					if (pcm_offset == 0) {
						// if we haven't got any on-time audio yet, then the audio_time counter is meaningless.
						data.audio_time = NAN;
					}
					return pcm_offset;
				}
				ret = avcodec_send_packet(data.acodec_ctx, &pkt);
				if (ret < 0) {
					ERR_PRINT(vformat("avcodec_send_packet returns %d", ret));
					av_packet_unref(&pkt);
					return pcm_offset;
				}
				av_packet_unref(&pkt);
				goto retry_audio;
			} else if (ret < 0) {
				ERR_PRINT(vformat("avcodec_receive_frame returns %d", ret));
				return pcm_buffer_size - pcm_remaining;
			}
			// only set the audio frame time if this is the first frame we've decoded during this update.
			// any remaining frames are going into a buffer anyways
			p_time = data.audio_frame->pts * av_q2d(data.format_ctx->streams[data.audiostream_idx]->time_base);
			if (first_frame) {
				data.audio_time = p_time;
				first_frame = false;
			}
			// decoded audio ready here
			data.num_decoded_samples = swr_convert(data.swr_ctx, (uint8_t **)&data.audio_buffer, data.audio_frame->nb_samples, (const uint8_t **)data.audio_frame->extended_data, data.audio_frame->nb_samples);
			// data.num_decoded_samples = _interleave_audio_frame(data.audio_buffer, data.audio_frame);
			data.audio_buffer_pos = 0;
		}
		if (audio_reset) {
			if (data.time - p_time > data.diff_tolerance) {
				// skip samples if the frame time is too far in the past
				data.num_decoded_samples = 0;
			} else if (p_time > data.time) {
				// don't send any pcm data if the first frame hasn't started yet
				data.audio_time = NAN;
				break;
			}
		}
		sample_count = pcm_remaining < data.num_decoded_samples ? pcm_remaining : data.num_decoded_samples;
		if (sample_count > 0) {
			memcpy(pcm.ptrw() + pcm_offset * data.acodec_ctx->channels, data.audio_buffer + data.acodec_ctx->channels * data.audio_buffer_pos, sizeof(float) * sample_count * data.acodec_ctx->channels);
			pcm_offset += sample_count;
			pcm_remaining -= sample_count;
			data.num_decoded_samples -= sample_count;
			data.audio_buffer_pos += sample_count;
		}
	}

	return pcm_offset;
}

void VideoStreamFFMPEG::_bind_methods() {}

Variant VideoStreamFFMPEGLoader::_load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	if (f.is_null()) {
		return Ref<Resource>();
	}

	Ref<VideoStreamFFMPEG> stream;
	stream.instantiate();
	stream->set_file(p_path);

	return stream;
}

PackedStringArray VideoStreamFFMPEGLoader::_get_recognized_extensions() const {
	PackedStringArray arr;
	arr.push_back("mp4");
	return arr;
}

bool VideoStreamFFMPEGLoader::_handles_type(const StringName &p_type) const {
	return p_type.to_lower() == "videostream";
}

String VideoStreamFFMPEGLoader::_get_resource_type(const String &p_path) const {
	String extension = p_path.get_extension().to_lower();
	if (extension == "mp4") {
		return "VideoStreamFFMPEG";
	}
	return "";
}