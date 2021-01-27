#ifdef _MSC_VER
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <time.h>
#include <stdint.h>
#include <string.h>

#include <gdnative_api_struct.gen.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include "packet_queue.h"
#include "set.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach/mach_time.h>
#endif

// TODO: is this sample rate defined somewhere in the godot api etc?
#define AUDIO_MIX_RATE 22050

enum POSITION_TYPE {POS_V_PTS, POS_TIME, POS_A_TIME};
typedef struct videodecoder_data_struct {

	godot_object *instance; // Don't clean
	AVIOContext *io_ctx;
	AVFormatContext *format_ctx;
	AVCodecContext *vcodec_ctx;
	AVFrame *frame_yuv;
	AVFrame *frame_rgb;

	struct SwsContext *sws_ctx;
	uint8_t *frame_buffer;

	int videostream_idx;
	int frame_buffer_size;
	godot_pool_byte_array unwrapped_frame;
	godot_real time;

	double audio_time;
	double diff_tolerance;

	int audiostream_idx;
	AVCodecContext *acodec_ctx;
	godot_bool acodec_open;
	AVFrame *audio_frame;
	void *mix_udata;

	int num_decoded_samples;
	float *audio_buffer;
	int audio_buffer_pos;

	SwrContext *swr_ctx;

	PacketQueue *audio_packet_queue;
	PacketQueue *video_packet_queue;

	unsigned long drop_frame;
	unsigned long total_frame;

	double seek_time;

	enum POSITION_TYPE position_type;
	uint8_t *io_buffer;
	godot_bool vcodec_open;
	godot_bool input_open;
	bool frame_unwrapped;

} videodecoder_data_struct;

const godot_int IO_BUFFER_SIZE = 512 * 1024; // File reading buffer of 512 KiB
const godot_int AUDIO_BUFFER_MAX_SIZE = 192000;

const godot_gdnative_core_api_struct *api = NULL;
const godot_gdnative_ext_nativescript_api_struct *nativescript_api = NULL;
const godot_gdnative_ext_nativescript_1_1_api_struct *nativescript_api_1_1 = NULL;
const godot_gdnative_ext_videodecoder_api_struct *videodecoder_api = NULL;

extern const godot_videodecoder_interface_gdnative plugin_interface;

static const char *plugin_name = "ffmpeg_videoplayer";
static int num_supported_ext = 0;
static char **supported_ext = NULL;

/// Clock Setup function (used by get_ticks_usec)
static uint64_t _clock_start = 0;
#if defined(__APPLE__)
static double _clock_scale = 0;
static void _setup_clock() {
	mach_timebase_info_data_t info;
	kern_return_t ret = mach_timebase_info(&info);
	_clock_scale = ((double)info.numer / (double)info.denom) / 1000.0;
	_clock_start = mach_absolute_time() * _clock_scale;
}
#elif defined(_MSC_VER)
uint64_t ticks_per_second;
uint64_t ticks_start;
static uint64_t get_ticks_usec();
static void _setup_clock() {
	// We need to know how often the clock is updated
	if (!QueryPerformanceFrequency((LARGE_INTEGER *)&ticks_per_second))
		ticks_per_second = 1000;
	ticks_start = 0;
	ticks_start = get_ticks_usec();
}
#else
#if defined(CLOCK_MONOTONIC_RAW) && !defined(JAVASCRIPT_ENABLED) // This is a better clock on Linux.
#define GODOT_CLOCK CLOCK_MONOTONIC_RAW
#else
#define GODOT_CLOCK CLOCK_MONOTONIC
#endif
static void _setup_clock() {
	struct timespec tv_now = { 0, 0 };
	clock_gettime(GODOT_CLOCK, &tv_now);
	_clock_start = ((uint64_t)tv_now.tv_nsec / 1000L) + (uint64_t)tv_now.tv_sec * 1000000L;
}
#endif
static uint64_t get_ticks_usec() {
#if defined(_MSC_VER)

	uint64_t ticks;

	// This is the number of clock ticks since start
	if (!QueryPerformanceCounter((LARGE_INTEGER *)&ticks))
		ticks = (UINT64)timeGetTime();

	// Divide by frequency to get the time in seconds
	// original calculation shown below is subject to overflow
	// with high ticks_per_second and a number of days since the last reboot.
	// time = ticks * 1000000L / ticks_per_second;

	// we can prevent this by either using 128 bit math
	// or separating into a calculation for seconds, and the fraction
	uint64_t seconds = ticks / ticks_per_second;

	// compiler will optimize these two into one divide
	uint64_t leftover = ticks % ticks_per_second;

	// remainder
	uint64_t time = (leftover * 1000000L) / ticks_per_second;

	// seconds
	time += seconds * 1000000L;

	// Subtract the time at game start to get
	// the time since the game started
	time -= ticks_start;
	return time;
#else
	#if defined(__APPLE__)
	uint64_t longtime = mach_absolute_time() * _clock_scale;
	#else
	struct timespec tv_now = { 0, 0 };
	clock_gettime(GODOT_CLOCK, &tv_now);
	uint64_t longtime = ((uint64_t)tv_now.tv_nsec / 1000L) + (uint64_t)tv_now.tv_sec * 1000000L;
	#endif
	longtime -= _clock_start;

	return longtime;
#endif
}

static uint64_t get_ticks_msec() {
	return get_ticks_usec() / 1000L;
}

#define STRINGIFY(x) #x
#define PROFILE_START(sig, line) const char __profile_sig__[] = "gdnative_videodecoder.c::" STRINGIFY(line) "::" sig; \
	uint64_t __profile_ticks_start__ = get_ticks_usec()

#define PROFILE_END if (nativescript_api_1_1) \
	nativescript_api_1_1->godot_nativescript_profiling_add_data( \
	__profile_sig__, get_ticks_usec() - __profile_ticks_start__ \
)

// Cleanup should empty the struct to the point where you can open a new file from.
static void _cleanup(videodecoder_data_struct *data) {

	if (data->audio_packet_queue != NULL) {
		packet_queue_deinit(data->audio_packet_queue);
		data->audio_packet_queue = NULL;
	}

	if (data->video_packet_queue != NULL) {
		packet_queue_deinit(data->video_packet_queue);
		data->video_packet_queue = NULL;
	}

	if (data->sws_ctx != NULL) {
		sws_freeContext(data->sws_ctx);
		data->sws_ctx = NULL;
	}

	if (data->audio_frame != NULL) {
		av_frame_unref(data->audio_frame);
		data->audio_frame = NULL;
	}

	if (data->frame_rgb != NULL) {
		av_frame_unref(data->frame_rgb);
		data->frame_rgb = NULL;
	}

	if (data->frame_yuv != NULL) {
		av_frame_unref(data->frame_yuv);
		data->frame_yuv = NULL;
	}

	if (data->frame_buffer != NULL) {
		api->godot_free(data->frame_buffer);
		data->frame_buffer = NULL;
		data->frame_buffer_size = 0;
	}

	if (data->vcodec_ctx != NULL) {
		if (data->vcodec_open) {
			avcodec_close(data->vcodec_ctx);
			data->vcodec_open = GODOT_FALSE;
		}
		avcodec_free_context(&data->vcodec_ctx);
		data->vcodec_ctx = NULL;
	}

	if (data->acodec_ctx != NULL) {
		if (data->acodec_open) {
			avcodec_close(data->acodec_ctx);
			data->vcodec_open = GODOT_FALSE;
			avcodec_free_context(&data->acodec_ctx);
			data->acodec_ctx = NULL;
		}
	}

	if (data->format_ctx != NULL) {
		if (data->input_open) {
			avformat_close_input(&data->format_ctx);
			data->input_open = GODOT_FALSE;
		}
		avformat_free_context(data->format_ctx);
		data->format_ctx = NULL;
	}

	if (data->io_ctx != NULL) {
		avio_context_free(&data->io_ctx);
		data->io_ctx = NULL;
	}

	if (data->io_buffer != NULL) {
		api->godot_free(data->io_buffer);
		data->io_buffer = NULL;
	}

	if (data->audio_buffer != NULL) {
		api->godot_free(data->audio_buffer);
		data->audio_buffer = NULL;
	}

	if (data->swr_ctx != NULL) {
		swr_free(&data->swr_ctx);
		data->swr_ctx = NULL;
	}

	data->time = 0;
	data->seek_time = 0;
	data->diff_tolerance = 0;
	data->videostream_idx = -1;
	data->audiostream_idx = -1;
	data->num_decoded_samples = 0;
	data->audio_buffer_pos = 0;

	data->drop_frame = data->total_frame = 0;
}

static void _unwrap_video_frame(godot_pool_byte_array *dest, AVFrame *frame, int width, int height) {
	int frame_size = width * height * 4;
	if (api->godot_pool_byte_array_size(dest) != frame_size) {
		api->godot_pool_byte_array_resize(dest, frame_size);
	}

	godot_pool_byte_array_write_access *write_access = api->godot_pool_byte_array_write(dest);
	uint8_t *write_ptr = api->godot_pool_byte_array_write_access_ptr(write_access);
	int val = 0;
	for (int y = 0; y < height; y++) {
		memcpy(write_ptr, frame->data[0] + y * frame->linesize[0], width * 4);
		write_ptr += width * 4;
	}
	api->godot_pool_byte_array_write_access_destroy(write_access);
}

static int _interleave_audio_frame(float *dest, AVFrame *audio_frame) {
	float **audio_frame_data = (float **)audio_frame->data;
	int count = 0;
	for (int j = 0; j != audio_frame->nb_samples; j++) {
		for (int i = 0; i != audio_frame->channels; i++) {
			dest[count++] = audio_frame_data[i][j];
		}
	}
	return audio_frame->nb_samples;
}

static void _update_extensions() {
	if (num_supported_ext > 0) return;

	const AVInputFormat *current_fmt = NULL;
	set_t *sup_ext_set = NULL;
	void *iterator_opaque = NULL;
	while ((current_fmt = av_demuxer_iterate(&iterator_opaque)) != NULL) {
		if (current_fmt->extensions != NULL) {
			char *exts = (char *)api->godot_alloc(strlen(current_fmt->extensions) + 1);
			strcpy(exts, current_fmt->extensions);
			char *token = strtok(exts, ",");
			while (token != NULL) {
				sup_ext_set = set_insert(sup_ext_set, token);
				token = strtok(NULL, ", ");
			}
			api->godot_free(exts);
			if (current_fmt->mime_type) {
				char *mime_types = (char *)api->godot_alloc(strlen(current_fmt->mime_type) + 1);
				strcpy(mime_types, current_fmt->mime_type);
				char *token = strtok(mime_types, ",");
				// for some reason the webm extension is missing from the format that supports it
				while (token != NULL) {
					if (strcmp("video/webm", token) == 0) {
						sup_ext_set = set_insert(sup_ext_set, "webm");
					}
					token = strtok(NULL, ",");
				}
				api->godot_free(mime_types);
			}
		}
	}

	list_t ext_list = set_create_list(sup_ext_set);
	num_supported_ext = list_size(&ext_list);
	supported_ext = (char **)api->godot_alloc(sizeof(char *) * num_supported_ext);
	list_node_t *cur_node = ext_list.start;
	int i = 0;
	while (cur_node != NULL) {
		supported_ext[i] = cur_node->value;
		cur_node->value = NULL;
		cur_node = cur_node->next;
		i++;
	}
	list_free(&ext_list);
	set_free(sup_ext_set);
}

static inline godot_real _avtime_to_sec(int64_t avtime) {
	return avtime / (godot_real)AV_TIME_BASE;
}

static void _godot_print(char *msg) {
	godot_string g_msg = {0};
	g_msg = api->godot_string_chars_to_utf8(msg);
	api->godot_print(&g_msg);
	api->godot_string_destroy(&g_msg);
}

static void print_codecs() {

	const AVCodecDescriptor *desc = NULL;
	unsigned nb_codecs = 0, i = 0;

	char msg[512] = {0};
	snprintf(msg, sizeof(msg) - 1, "%s: Supported video codecs:", plugin_name);
	_godot_print(msg);
	while ((desc = avcodec_descriptor_next(desc))) {
		const AVCodec* codec = NULL;
		void* i = NULL;
		bool found = false;
		while ((codec = av_codec_iterate(&i))) {
			if (codec->id == desc->id && av_codec_is_decoder(codec)) {
				if (!found && avcodec_find_decoder(desc->id) || avcodec_find_encoder(desc->id)) {

					snprintf(msg, sizeof(msg) - 1, "\t%s%s%s",
						avcodec_find_decoder(desc->id) ? "decode " : "",
						avcodec_find_encoder(desc->id) ? "encode " : "",
						desc->name
					);
					found = true;
					_godot_print(msg);
				}
				if (strcmp(codec->name, desc->name) != 0) {
					snprintf(msg, sizeof(msg) - 1, "\t  codec: %s", codec->name);
					_godot_print(msg);
				}
			}
		}
	}
}

inline static bool api_ver(godot_gdnative_api_version v, unsigned int want_major, unsigned int want_minor) {
	return v.major == want_major && v.minor == want_minor;
}


void GDN_EXPORT godot_gdnative_init(godot_gdnative_init_options *p_options) {
	_setup_clock();
	api = p_options->api_struct;
	for (int i = 0; i < api->num_extensions; i++) {
		switch (api->extensions[i]->type) {
			case GDNATIVE_EXT_VIDEODECODER:
				videodecoder_api = (godot_gdnative_ext_videodecoder_api_struct *)api->extensions[i];

				break;
			case GDNATIVE_EXT_NATIVESCRIPT:
				nativescript_api = (godot_gdnative_ext_nativescript_api_struct *)api->extensions[i];
				const godot_gdnative_api_struct *ext_next = nativescript_api->next;
				while (ext_next) {
					if (api_ver(ext_next->version, 1, 1)) {
						nativescript_api_1_1 = (godot_gdnative_ext_nativescript_1_1_api_struct *)ext_next;
						break;
					}
					ext_next = ext_next->next;
				}
				break;
			default: break;
		}
	}
	print_codecs();
}

void GDN_EXPORT godot_gdnative_terminate(godot_gdnative_terminate_options *p_options) {
	api = NULL;
}

void GDN_EXPORT godot_gdnative_singleton() {
	if (videodecoder_api != NULL) {
		videodecoder_api->godot_videodecoder_register_decoder(&plugin_interface);
	}
}

void *godot_videodecoder_constructor(godot_object *p_instance) {
	videodecoder_data_struct *data = api->godot_alloc(sizeof(videodecoder_data_struct));

	data->instance = p_instance;

	data->io_buffer = NULL;
	data->io_ctx = NULL;

	data->format_ctx = NULL;
	data->input_open = GODOT_FALSE;

	data->videostream_idx = -1;
	data->vcodec_ctx = NULL;
	data->vcodec_open = GODOT_FALSE;

	data->frame_rgb = NULL;
	data->frame_yuv = NULL;
	data->sws_ctx = NULL;

	data->frame_buffer = NULL;
	data->frame_buffer_size = 0;

	data->audiostream_idx = -1;
	data->acodec_ctx = NULL;
	data->acodec_open = GODOT_FALSE;
	data->audio_frame = NULL;
	data->audio_buffer = NULL;

	data->swr_ctx = NULL;

	data->num_decoded_samples = 0;
	data->audio_buffer_pos = 0;

	data->audio_packet_queue = NULL;
	data->video_packet_queue = NULL;

	data->position_type = POS_A_TIME;
	data->time = 0;
	data->audio_time = NAN;

	data->frame_unwrapped = false;
	api->godot_pool_byte_array_new(&data->unwrapped_frame);

	return data;
}

void godot_videodecoder_destructor(void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	_cleanup(data);

	data->instance = NULL;
	api->godot_pool_byte_array_destroy(&data->unwrapped_frame);

	api->godot_free(data);
	data = NULL; // Not needed, but just to be safe.

	if (num_supported_ext > 0) {
		for (int i = 0; i < num_supported_ext; i++) {
			if (supported_ext[i] != NULL) {
				api->godot_free((void *)supported_ext[i]);
			}
		}
		api->godot_free(supported_ext);
		num_supported_ext = 0;
	}
}

const char **godot_videodecoder_get_supported_ext(int *p_count) {
	_update_extensions();
	*p_count = num_supported_ext;
	return (const char **)supported_ext;
}

const char *godot_videodecoder_get_plugin_name(void) {
	return plugin_name;
}

godot_bool godot_videodecoder_open_file(void *p_data, void *file) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	// Clean up the previous file.
	_cleanup(data);

	data->io_buffer = (uint8_t *)api->godot_alloc(IO_BUFFER_SIZE * sizeof(uint8_t));
	if (data->io_buffer == NULL) {
		_cleanup(data);
		api->godot_print_warning("Buffer alloc error", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	godot_int read_bytes = videodecoder_api->godot_videodecoder_file_read(file, data->io_buffer, IO_BUFFER_SIZE);

	// Rewind to 0
	videodecoder_api->godot_videodecoder_file_seek(file, 0, SEEK_SET);

	// Determine input format
	AVProbeData probe_data;
	probe_data.buf = data->io_buffer;
	probe_data.buf_size = read_bytes;
	probe_data.filename = "";
	probe_data.mime_type = "";

	AVInputFormat *input_format = NULL;
	input_format = av_probe_input_format(&probe_data, 1);
	if (input_format == NULL) {
		_cleanup(data);
		char msg[512] = {0};
		snprintf(msg, sizeof(msg) - 1, "Format not recognized: %s (%s)", probe_data.filename, probe_data.mime_type);
		api->godot_print_error(msg, "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}
	input_format->flags |= AVFMT_SEEK_TO_PTS;

	data->io_ctx = avio_alloc_context(data->io_buffer, IO_BUFFER_SIZE, 0, file,
			videodecoder_api->godot_videodecoder_file_read, NULL,
			videodecoder_api->godot_videodecoder_file_seek);
	if (data->io_ctx == NULL) {
		_cleanup(data);
		api->godot_print_error("IO context alloc error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->format_ctx = avformat_alloc_context();
	if (data->format_ctx == NULL) {
		_cleanup(data);
		api->godot_print_error("Format context alloc error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->format_ctx->pb = data->io_ctx;
	data->format_ctx->flags = AVFMT_FLAG_CUSTOM_IO;
	data->format_ctx->iformat = input_format;

	if (avformat_open_input(&data->format_ctx, "", NULL, NULL) != 0) {
		_cleanup(data);
		api->godot_print_error("Input stream failed to open", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}
	data->input_open = GODOT_TRUE;

	if (avformat_find_stream_info(data->format_ctx, NULL) < 0) {
		_cleanup(data);
		api->godot_print_error("Could not find stream info.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->videostream_idx = -1; // should be -1 anyway, just being paranoid.
	data->audiostream_idx = -1;
	// find stream
	for (int i = 0; i < data->format_ctx->nb_streams; i++) {
		if (data->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			data->videostream_idx = i;
		} else if (data->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			data->audiostream_idx = i;
		}
	}
	if (data->videostream_idx == -1) {
		_cleanup(data);
		api->godot_print_error("Video Stream not found.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	AVCodecParameters *vcodec_param = data->format_ctx->streams[data->videostream_idx]->codecpar;

	AVCodec *vcodec = NULL;
	vcodec = avcodec_find_decoder(vcodec_param->codec_id);
	if (vcodec == NULL) {
		const AVCodecDescriptor *desc = avcodec_descriptor_get(vcodec_param->codec_id);
		char msg[512] = {0};
		snprintf(msg, sizeof(msg) - 1, "Videodecoder %s (%s) not found.", desc->name, desc->long_name);
		api->godot_print_warning(msg, "godot_videodecoder_open_file()", __FILE__, __LINE__);
		_cleanup(data);
		return GODOT_FALSE;
	}

	data->vcodec_ctx = avcodec_alloc_context3(vcodec);
	if (data->vcodec_ctx == NULL) {
		_cleanup(data);
		api->godot_print_warning("Videocodec allocation error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	if (avcodec_parameters_to_context(data->vcodec_ctx, vcodec_param) < 0) {
		_cleanup(data);
		api->godot_print_warning("Videocodec context init error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}
	// enable multi-thread decoding based on CPU core count
	data->vcodec_ctx->thread_count = 0;

	if (avcodec_open2(data->vcodec_ctx, vcodec, NULL) < 0) {
		_cleanup(data);
		api->godot_print_warning("Videocodec failed to open.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}
	data->vcodec_open = GODOT_TRUE;


	AVCodecParameters *acodec_param = NULL;
	AVCodec *acodec = NULL;
	if (data->audiostream_idx >= 0) {
		acodec_param = data->format_ctx->streams[data->audiostream_idx]->codecpar;

		acodec = avcodec_find_decoder(acodec_param->codec_id);
		if (acodec == NULL) {
			const AVCodecDescriptor *desc = avcodec_descriptor_get(acodec_param->codec_id);
			char msg[512] = {0};
			snprintf(msg, sizeof(msg) - 1, "Audiodecoder %s (%s) not found.", desc-> name, desc->long_name);
			api->godot_print_warning(msg, "godot_videodecoder_open_file()", __FILE__, __LINE__);
			_cleanup(data);
			return GODOT_FALSE;
		}
		data->acodec_ctx = avcodec_alloc_context3(acodec);
		if (data->acodec_ctx == NULL) {
			_cleanup(data);
			api->godot_print_error("Audiocodec allocation error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
			return GODOT_FALSE;
		}

		if (avcodec_parameters_to_context(data->acodec_ctx, acodec_param) < 0) {
			_cleanup(data);
			api->godot_print_error("Audiocodec context init error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
			return GODOT_FALSE;
		}

		if (avcodec_open2(data->acodec_ctx, acodec, NULL) < 0) {
			_cleanup(data);
			api->godot_print_error("Audiocodec failed to open.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
			return GODOT_FALSE;
		}
		data->acodec_open = GODOT_TRUE;

		data->audio_buffer = (float *)api->godot_alloc(AUDIO_BUFFER_MAX_SIZE * sizeof(float));
		if (data->audio_buffer == NULL) {
			_cleanup(data);
			api->godot_print_error("Audio buffer alloc failed.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
			return GODOT_FALSE;
		}

		data->audio_frame = av_frame_alloc();
		if (data->audio_frame == NULL) {
			_cleanup(data);
			api->godot_print_error("Frame alloc fail.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
			return GODOT_FALSE;
		}

		data->swr_ctx = swr_alloc();
		av_opt_set_int(data->swr_ctx, "in_channel_layout", data->acodec_ctx->channel_layout, 0);
		av_opt_set_int(data->swr_ctx, "out_channel_layout", data->acodec_ctx->channel_layout, 0);
		av_opt_set_int(data->swr_ctx, "in_sample_rate", data->acodec_ctx->sample_rate, 0);
		av_opt_set_int(data->swr_ctx, "out_sample_rate", AUDIO_MIX_RATE, 0);
		av_opt_set_sample_fmt(data->swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
		av_opt_set_sample_fmt(data->swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
		swr_init(data->swr_ctx);
	}

	// NOTE: Align of 1 (I think it is for 32 bit alignment.) Doesn't work otherwise
	data->frame_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGB32,
			data->vcodec_ctx->width, data->vcodec_ctx->height, 1);

	data->frame_buffer = (uint8_t *)api->godot_alloc(data->frame_buffer_size);
	if (data->frame_buffer == NULL) {
		_cleanup(data);
		api->godot_print_error("Framebuffer alloc fail.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->frame_rgb = av_frame_alloc();
	if (data->frame_rgb == NULL) {
		_cleanup(data);
		api->godot_print_error("Frame alloc fail.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->frame_yuv = av_frame_alloc();
	if (data->frame_yuv == NULL) {
		_cleanup(data);
		api->godot_print_error("Frame alloc fail.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	int width = data->vcodec_ctx->width;
	int height = data->vcodec_ctx->height;
	if (av_image_fill_arrays(data->frame_rgb->data, data->frame_rgb->linesize, data->frame_buffer,
				AV_PIX_FMT_RGB32, width, height, 1) < 0) {
		_cleanup(data);
		api->godot_print_error("Frame fill.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->sws_ctx = sws_getContext(width, height, data->vcodec_ctx->pix_fmt,
			width, height, AV_PIX_FMT_RGB0, SWS_BILINEAR,
			NULL, NULL, NULL);
	if (data->sws_ctx == NULL) {
		_cleanup(data);
		api->godot_print_error("Swscale context not created.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->time = 0;
	data->num_decoded_samples = 0;

	data->audio_packet_queue = packet_queue_init();
	data->video_packet_queue = packet_queue_init();

	data->drop_frame = data->total_frame = 0;

	return GODOT_TRUE;
}

godot_real godot_videodecoder_get_length(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	if (data->format_ctx == NULL) {
		api->godot_print_warning("Format context is null.", "godot_videodecoder_get_length()", __FILE__, __LINE__);
		return -1;
	}

	return data->format_ctx->streams[data->videostream_idx]->duration * av_q2d(data->format_ctx->streams[data->videostream_idx]->time_base);
}

static bool read_frame(videodecoder_data_struct *data) {
	while (data->video_packet_queue->nb_packets < 24)  {
		AVPacket pkt;
		int ret = av_read_frame(data->format_ctx, &pkt);
		if (ret >= 0) {
			if (pkt.stream_index == data->videostream_idx) {
				packet_queue_put(data->video_packet_queue, &pkt);
			} else if (pkt.stream_index == data->audiostream_idx) {
				packet_queue_put(data->audio_packet_queue, &pkt);
			} else {
				av_packet_unref(&pkt);
			}
		} else {
			return false;
		}
	}
	return true;
}

void godot_videodecoder_update(void *p_data, godot_real p_delta) {
	PROFILE_START("update", __LINE__);
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// during an 'update' make sure to use the video frame's pts timestamp
	// otherwise the godot VideoStreamNative update method
	// won't even try to request a frame since it expects the plugin
	// to use video presentation timestamp as the source of time.

	data->position_type = POS_V_PTS;

	data->time += p_delta;
	// afford one frame worth of slop when decoding
	data->diff_tolerance = p_delta;

	if (!isnan(data->audio_time)) {
		data->audio_time += p_delta;
	}
	read_frame(data);
	PROFILE_END;
}

godot_pool_byte_array *godot_videodecoder_get_videoframe(void *p_data) {
	PROFILE_START("get_videoframe", __LINE__);
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	AVPacket pkt = {0};
	int ret;
	size_t drop_count = 0;
	// to maintain a decent game frame rate
	// don't let frame decoding take more than this number of ms
	uint64_t max_frame_drop_time = 5;
	// but we do need to drop frames, so try to drop at least some frames even if it's a bit slow :(
	size_t min_frame_drop_count = 5;
	uint64_t start = get_ticks_msec();

retry:
	ret = avcodec_receive_frame(data->vcodec_ctx, data->frame_yuv);
	if (ret == AVERROR(EAGAIN)) {
		// need to call avcodedc_send_packet, get a packet from queue to send it
		while (!packet_queue_get(data->video_packet_queue, &pkt)) {
			//api->godot_print_warning("video packet queue empty", "godot_videodecoder_get_videoframe()", __FILE__, __LINE__);
			if (!read_frame(data)) {
				PROFILE_END;
				return NULL;
			}
		}
		ret = avcodec_send_packet(data->vcodec_ctx, &pkt);
		if (ret < 0) {
			char err[512];
			char msg[768];
			av_strerror(ret, err, sizeof(err) - 1);
			snprintf(msg, sizeof(msg) - 1, "avcodec_send_packet returns %d (%s)", ret, err);
			api->godot_print_error(msg, "godot_videodecoder_get_videoframe()", __FILE__, __LINE__);
			av_packet_unref(&pkt);
			PROFILE_END;
			return NULL;
		}
		av_packet_unref(&pkt);
		goto retry;
	} else if (ret < 0) {
		char msg[512] = {0};
		snprintf(msg, sizeof(msg) - 1, "avcodec_receive_frame returns %d", ret);
		api->godot_print_error(msg, "godot_videodecoder_get_videoframe()", __FILE__, __LINE__);
		PROFILE_END;
		return NULL;
	}

	bool pts_correct = data->frame_yuv->pts == AV_NOPTS_VALUE;
	int64_t pts = pts_correct ? data->frame_yuv->pkt_dts : data->frame_yuv->pts;

	double ts = pts * av_q2d(data->format_ctx->streams[data->videostream_idx]->time_base);

	data->total_frame++;

	// frame successfully decoded here, now if it lags behind too much (diff_tolerance sec)
	// let's discard this frame and get the next frame instead
	bool drop = ts < data->time - data->diff_tolerance;
	uint64_t drop_duration = get_ticks_msec() - start;
	if (drop && drop_duration > max_frame_drop_time && drop_count < min_frame_drop_count && data->frame_unwrapped) {
		// only discard frames for max_frame_drop_time ms or we'll slow down the game's main thread!
		if (fabs(data->seek_time - data->time) > data->diff_tolerance * 10) {
			char msg[512];
			snprintf(msg, sizeof(msg) -1, "Slow CPU? Dropped  %d frames for %"PRId64"ms frame dropped: %lu/%lu (%.1f%%) pts=%.1f t=%.1f",
				(int)drop_count,
				drop_duration,
				data->drop_frame,
				data->total_frame,
				100.0 * data->drop_frame / data->total_frame,
				ts, (double)data->time);
			api->godot_print_warning(msg, "godot_videodecoder_get_videoframe()", __FILE__, __LINE__);
		}
	} else if (drop) {
		drop_count++;
		data->drop_frame++;
		av_packet_unref(&pkt);
		goto retry;
	}
	if (!drop || fabs(data->seek_time - data->time) > data->diff_tolerance * 2) {
		// Don't overwrite the current frame when dropping frames for performance reasons
		// except when the time is within 2 frames of the most recent seek
		// because we don't want a glitchy 'fast forward' effect when seeking.
		// NOTE: VideoPlayer currently doesnt' ask for a frame when seeking while paused so you'd
		// have to fake it inside godot by unpausing briefly. (see FIG1 below)
		data->frame_unwrapped = true;
		sws_scale(data->sws_ctx, (uint8_t const *const *)data->frame_yuv->data, data->frame_yuv->linesize, 0,
				data->vcodec_ctx->height, data->frame_rgb->data, data->frame_rgb->linesize);
		_unwrap_video_frame(&data->unwrapped_frame, data->frame_rgb, data->vcodec_ctx->width, data->vcodec_ctx->height);
	}
	av_packet_unref(&pkt);

	// hack to get video_stream_gdnative to stop asking for frames.
	// stop trusting video pts until the next time update() is called.
	// this will unblock VideoStreamPlaybackGDNative::update() which
	// keeps calling get_texture() until the time matches
	// we don't need this behavior as we already handle frame skipping internally.
	data->position_type = POS_TIME;
	PROFILE_END;
	return data->frame_unwrapped ? &data->unwrapped_frame : NULL;
}

/*
FIG1: how to seek while paused...

var _paused_seeking = 0
func seek_player(value):
	var was_playing = _playing
	if _playing:
		stop()
	_player.stream_position = value

	if was_playing:
		play(value)
		if _player.paused || _paused_seeking > 0:
			_player.paused = false
			_paused_seeking = _paused_seeking + 1
			# yes, it seems like 5 idle frames _is_ the magic number.
			# VideoPlayer gets notified to do NOTIFICATION_INTERNAL_PROCESS on idle frames
			# so this should always work?
			for i in range(5):
				yield(get_tree(), 'idle_frame')
			# WARNING: -= double decrements here somehow?
			_paused_seeking = _paused_seeking - 1
			assert(_paused_seeking >= 0)
			if _paused_seeking == 0:
				_player.paused = true

*/

godot_int godot_videodecoder_get_audio(void *p_data, float *pcm, int pcm_remaining) {
	PROFILE_START("get_audio", __LINE__);
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	if (data->audiostream_idx < 0) {
		PROFILE_END;
		return 0;
	}
	bool first_frame = true;

	// if playback has just started or just seeked then we enter the audio_reset state.
	// during audio_reset it's important to skip old samples
	// _and_ avoid sending samples from the future until the presentation timestamp syncs up.
	bool audio_reset = isnan(data->audio_time) || data->audio_time > data->time - data->diff_tolerance;

	const int pcm_buffer_size = pcm_remaining;
	int pcm_offset = 0;

	double p_time = data->audio_frame->pts * av_q2d(data->format_ctx->streams[data->audiostream_idx]->time_base);

	if (audio_reset && data->num_decoded_samples > 0) {
		// don't send any pcm data if the frame hasn't started yet
		if (p_time > data->time) {
			PROFILE_END;
			return 0;
		}
		// skip the any decoded samples if their presentation timestamp is too old
		if (data->time - p_time > data->diff_tolerance) {
			data->num_decoded_samples = 0;
		}
	}

	int sample_count = (pcm_remaining < data->num_decoded_samples) ? pcm_remaining : data->num_decoded_samples;

	if (sample_count > 0) {
		memcpy(pcm, data->audio_buffer + data->acodec_ctx->channels * data->audio_buffer_pos, sizeof(float) * sample_count * data->acodec_ctx->channels);
		pcm_offset += sample_count;
		pcm_remaining -= sample_count;
		data->num_decoded_samples -= sample_count;
		data->audio_buffer_pos += sample_count;
	}
	while (pcm_remaining > 0) {
		if (data->num_decoded_samples <= 0) {
			AVPacket pkt;

			int ret;
retry_audio:
			ret = avcodec_receive_frame(data->acodec_ctx, data->audio_frame);
			if (ret == AVERROR(EAGAIN)) {
				// need to call avcodec_send_packet, get a packet from queue to send it
				if (!packet_queue_get(data->audio_packet_queue, &pkt)) {
					if (pcm_offset == 0) {
						// if we haven't got any on-time audio yet, then the audio_time counter is meaningless.
						data->audio_time = NAN;
					}
					PROFILE_END;
					return pcm_offset;
				}
				ret = avcodec_send_packet(data->acodec_ctx, &pkt);
				if (ret < 0) {
					char msg[512];
					snprintf(msg, sizeof(msg) -1, "avcodec_send_packet returns %d", ret);
					api->godot_print_error(msg, "godot_videodecoder_get_audio()", __FILE__, __LINE__);
					av_packet_unref(&pkt);
					PROFILE_END;
					return pcm_offset;
				}
				av_packet_unref(&pkt);
				goto retry_audio;
			} else if (ret < 0) {
				char msg[512];
				snprintf(msg, sizeof(msg) - 1, "avcodec_receive_frame returns %d", ret);
				api->godot_print_error(msg, "godot_videodecoder_get_audio()", __FILE__, __LINE__);
				PROFILE_END;
				return pcm_buffer_size - pcm_remaining;
			}
			// only set the audio frame time if this is the first frame we've decoded during this update.
			// any remaining frames are going into a buffer anyways
			p_time = data->audio_frame->pts * av_q2d(data->format_ctx->streams[data->audiostream_idx]->time_base);
			if (first_frame) {
				data->audio_time = p_time;
				first_frame = false;
			}
			// decoded audio ready here
			data->num_decoded_samples = swr_convert(data->swr_ctx, (uint8_t **)&data->audio_buffer, data->audio_frame->nb_samples, (const uint8_t **)data->audio_frame->extended_data, data->audio_frame->nb_samples);
			// data->num_decoded_samples = _interleave_audio_frame(data->audio_buffer, data->audio_frame);
			data->audio_buffer_pos = 0;
		}
		if (audio_reset) {
			if (data->time - p_time > data->diff_tolerance) {
				// skip samples if the frame time is too far in the past
				data->num_decoded_samples = 0;
			} else if (p_time > data->time) {
				// don't send any pcm data if the first frame hasn't started yet
				data->audio_time = NAN;
				break;
			}
		}
		sample_count = pcm_remaining < data->num_decoded_samples ? pcm_remaining : data->num_decoded_samples;
		if (sample_count > 0) {
			memcpy(pcm + pcm_offset * data->acodec_ctx->channels, data->audio_buffer + data->acodec_ctx->channels * data->audio_buffer_pos, sizeof(float) * sample_count * data->acodec_ctx->channels);
			pcm_offset += sample_count;
			pcm_remaining -= sample_count;
			data->num_decoded_samples -= sample_count;
			data->audio_buffer_pos += sample_count;
		}
	}

	PROFILE_END;
	return pcm_offset;
}

godot_real godot_videodecoder_get_playback_position(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	if (data->format_ctx) {
		bool use_v_pts = data->frame_yuv->pts != AV_NOPTS_VALUE && data->position_type == POS_V_PTS;
		bool use_a_time = data->position_type == POS_A_TIME;
		bool in_update = data->position_type == POS_V_PTS;
		data->position_type = POS_TIME;

		if (use_v_pts) {
			double pts = (double)data->frame_yuv->pts;
			pts *= av_q2d(data->format_ctx->streams[data->videostream_idx]->time_base);
			return (godot_real)pts;
		} else {
			if (!isnan(data->audio_time) && use_a_time) {
				return (godot_real)data->audio_time;
			}
			// fudge the time if we in the first frame after an update but don't have V_PTS yet
			godot_real adjustment = in_update ? -0.01 : 0.0;
			return (godot_real)data->time + adjustment;
		}
	}
	return (godot_real)0;
}

static void flush_frames(AVCodecContext* ctx) {
	PROFILE_START("flush_frames", __LINE__);
	/**
	* from https://www.ffmpeg.org/doxygen/4.1/group__lavc__encdec.html
	* End of stream situations. These require "flushing" (aka draining) the codec, as the codec might buffer multiple frames or packets internally for performance or out of necessity (consider B-frames). This is handled as follows:
	* Instead of valid input, send NULL to the avcodec_send_packet() (decoding) or avcodec_send_frame() (encoding) functions. This will enter draining mode.
	* Call avcodec_receive_frame() (decoding) or avcodec_receive_packet() (encoding) in a loop until AVERROR_EOF is returned. The functions will not return AVERROR(EAGAIN), unless you forgot to enter draining mode.
	* Before decoding can be resumed again, the codec has to be reset with avcodec_flush_buffers().
	*/
	int ret = avcodec_send_packet(ctx, NULL);
	AVFrame frame = {0};
	if (ret <= 0) {
		do {
			ret = avcodec_receive_frame(ctx, &frame);
		} while (ret != AVERROR_EOF);
	}
	PROFILE_END;
}

void godot_videodecoder_seek(void *p_data, godot_real p_time) {
	PROFILE_START("seek", __LINE__);
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// Hack to find the end of the video. Really VideoPlayer should expose this!
	if (p_time < 0) {
		p_time = _avtime_to_sec(data->format_ctx->duration);
	}
	int64_t seek_target = p_time * AV_TIME_BASE;
	// seek within 10 seconds of the selected spot.
	int64_t margin = 10 * AV_TIME_BASE;

	// printf("seek(): %fs = %lld\n", p_time, seek_target);
	int ret = avformat_seek_file(data->format_ctx, -1, seek_target - margin, seek_target, seek_target, 0);
	if (ret < 0) {
		api->godot_print_warning("avformat_seek_file() can't seek backward?", "godot_videodecoder_seek()\n", __FILE__, __LINE__);
		ret = avformat_seek_file(data->format_ctx, -1, seek_target - margin, seek_target, seek_target + margin, 0);
	}
	if (ret < 0) {
		api->godot_print_error("avformat_seek_file() failed", "godot_videodecoder_seek()\n", __FILE__, __LINE__);
	} else {
		packet_queue_flush(data->video_packet_queue);
		packet_queue_flush(data->audio_packet_queue);
		flush_frames(data->vcodec_ctx);
		avcodec_flush_buffers(data->vcodec_ctx);
		if (data->acodec_ctx) {
			flush_frames(data->acodec_ctx);
			avcodec_flush_buffers(data->acodec_ctx);
		}
		data->num_decoded_samples = 0;
		data->audio_buffer_pos = 0;
		data->time = p_time;
		data->seek_time = p_time;
		// try to use the audio time as the seek position
		data->position_type = POS_A_TIME;
		data->audio_time = NAN;
	}
	PROFILE_END;
}

/* ---------------------- TODO ------------------------- */

void godot_videodecoder_set_audio_track(void *p_data, godot_int p_audiotrack) {
	// api->godot_print_warning("set_audio_track not implemented", "set_audio_track()\n", __FILE__, __LINE__);
}

godot_int godot_videodecoder_get_channels(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	if (data->acodec_ctx != NULL) {
		return data->acodec_ctx->channels;
	}
	return 0;
}

godot_int godot_videodecoder_get_mix_rate(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	if (data->acodec_ctx != NULL) {
		return AUDIO_MIX_RATE;
	}
	return 0;
}

godot_vector2 godot_videodecoder_get_texture_size(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	godot_vector2 vec;

	if (data->vcodec_ctx != NULL) {
		api->godot_vector2_new(&vec, data->vcodec_ctx->width, data->vcodec_ctx->height);
	}
	return vec;
}

const godot_videodecoder_interface_gdnative plugin_interface = {
	GODOTAV_API_MAJOR, GODOTAV_API_MINOR,
	NULL,
	godot_videodecoder_constructor,
	godot_videodecoder_destructor,
	godot_videodecoder_get_plugin_name,
	godot_videodecoder_get_supported_ext,
	godot_videodecoder_open_file,
	godot_videodecoder_get_length,
	godot_videodecoder_get_playback_position,
	godot_videodecoder_seek,
	godot_videodecoder_set_audio_track,
	godot_videodecoder_update,
	godot_videodecoder_get_videoframe,
	godot_videodecoder_get_audio,
	godot_videodecoder_get_channels,
	godot_videodecoder_get_mix_rate,
	godot_videodecoder_get_texture_size
};
