#ifndef VIDEO_STREAM_FFMPEG_H
#define VIDEO_STREAM_FFMPEG_H

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>
#include <godot_cpp/classes/video_stream.hpp>
#include <godot_cpp/classes/texture2d.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace godot {

class FFMPEGPacketQueue {

public:
	FFMPEGPacketQueue();
	~FFMPEGPacketQueue();

	void flush();
	int put(AVPacket *pkt);
	int get(AVPacket *pkt);

public:
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
};

class VideoStreamPlaybackFFMPEG : public VideoStreamPlayback {
	GDCLASS(VideoStreamPlaybackFFMPEG, VideoStreamPlayback);

private:
	enum POSITION_TYPE {
		POS_V_PTS,
		POS_TIME,
		POS_A_TIME
	};

	struct videodecoder_data_struct {
		AVIOContext *io_ctx;
		AVFormatContext *format_ctx;
		AVCodecContext *vcodec_ctx;
		AVFrame *frame_yuv;
		AVFrame *frame_rgb;

		struct SwsContext *sws_ctx;
		uint8_t *frame_buffer;

		int videostream_idx;
		int frame_buffer_size;
		PackedByteArray unwrapped_frame;
		double time;

		double audio_time;
		double diff_tolerance;

		int audiostream_idx;
		AVCodecContext *acodec_ctx;
		bool acodec_open;
		AVFrame *audio_frame;
		void *mix_udata;

		int num_decoded_samples;
		float *audio_buffer;
		int audio_buffer_pos;

		SwrContext *swr_ctx;

		FFMPEGPacketQueue *audio_packet_queue;
		FFMPEGPacketQueue *video_packet_queue;

		unsigned long drop_frame;
		unsigned long total_frame;

		double seek_time;

		enum POSITION_TYPE position_type;
		uint8_t *io_buffer;
		bool vcodec_open;
		bool input_open;
		bool frame_unwrapped;

		bool loaded;
	};

protected:
	static void _bind_methods();

public:
	static int raw_file_read(void *ptr, uint8_t *buf, int buf_size);
	static int64_t raw_file_seek(void *ptr, int64_t pos, int whence);

	virtual void _play() override;
	virtual void _stop() override;
	virtual bool _is_playing() const override;

	virtual void _set_paused(bool p_paused) override;
	virtual bool _is_paused() const override;

	virtual double _get_length() const override;

	virtual double _get_playback_position() const override;
	virtual void _seek(double p_time) override;

	void _set_file(const String &p_file);

	virtual Ref<Texture2D> _get_texture() const override;
	virtual void _update(double p_delta) override;

	virtual int _get_channels() const override;
	virtual int _get_mix_rate() const override;

	virtual void _set_audio_track(int p_idx) override;

	VideoStreamPlaybackFFMPEG();
	~VideoStreamPlaybackFFMPEG();

private:
	void _cleanup();
	void flush_frames(AVCodecContext* ctx);
	bool read_frame();
	void update_texture();
	PackedByteArray get_video_frame();
	int get_audio_frame(int pcm_remaining);

private:
	videodecoder_data_struct data;
	Ref<FileAccess> file;
	Ref<ImageTexture> texture;
	bool playing = false, paused = false;
	bool seek_backward = false;
	double delay_compensation = 0.0;
	double time = 0.0;
	PackedFloat32Array pcm;
	int pcm_write_idx;
	int samples_decoded;
};

class VideoStreamFFMPEG : public VideoStream {
	GDCLASS(VideoStreamFFMPEG, VideoStream);

	int audio_track = 0;

protected:
	static void _bind_methods();

public:
	Ref<VideoStreamPlayback> _instantiate_playback() override {
		Ref<VideoStreamPlaybackFFMPEG> pb = memnew(VideoStreamPlaybackFFMPEG);
		pb->_set_audio_track(audio_track);
		pb->_set_file(get_file());
		return pb;
	}

	VideoStreamFFMPEG() {}
	~VideoStreamFFMPEG() {}
};

class VideoStreamFFMPEGLoader : public ResourceFormatLoader {
	GDCLASS(VideoStreamFFMPEGLoader, ResourceFormatLoader);

protected:
	static void _bind_methods() {};

public:
	virtual Variant _load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode = CACHE_MODE_REUSE) const override;
	virtual PackedStringArray _get_recognized_extensions() const override;
	virtual bool _handles_type(const StringName &p_type) const override;
	virtual String _get_resource_type(const String &p_path) const override;

	VideoStreamFFMPEGLoader() {}
	~VideoStreamFFMPEGLoader() {}
};

} // namespace godot

#endif // VIDEO_STREAM_FFMPEG_H