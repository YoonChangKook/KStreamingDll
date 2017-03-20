#ifndef _MY_FFMPEG_STREAMER_H_
#define _MY_FFMPEG_STREAMER_H_

#ifdef KSTREAMINGDLL_EXPORTS
#define MY_FFMPEG_API __declspec(dllexport)
#else
#define MY_FFMPEG_API __declspec(dllimport)
#endif

#include <string>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
#include <libavdevice/avdevice.h>
}

#include <opencv2/core/core.hpp> // Basic OpenCV structures (cv::Mat)
#include <opencv2/imgproc/imgproc.hpp>

// ffmpeg
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swresample.lib")
#pragma comment(lib, "swscale.lib")

// opencv
#pragma comment(lib, "opencv_core2413.lib")
#pragma comment(lib, "opencv_core2413d.lib")
#pragma comment(lib, "opencv_imgproc2413.lib")
#pragma comment(lib, "opencv_imgproc2413d.lib")

#pragma warning(disable:4996)

#define STREAM_FPS		30
#define STREAM_PIX_FMT	AV_PIX_FMT_YUV420P

enum MyFFMPEGStreamerError{
	CANT_ALLOC_FORMAT_CONTEXT = 10, 
	CANT_ALLOC_OUTPUT_FORMAT = 11, 
	CANT_OPEN_RTSP_OUTPUT = 12, 
	CANT_WRITE_HEADER = 13, 
	NO_FFMPEG_ERROR = 100
};

class MY_FFMPEG_API MyFFMPEGStreamer
{
public:
	MyFFMPEGStreamer();
	~MyFFMPEGStreamer();

private:
	MyFFMPEGStreamerError last_error;
	// ffmpeg members
	std::string ip;
	int port;
	enum AVCodecID codec_id;
	AVOutputFormat *fmt;
	AVFormatContext *oc;
	AVStream *video_st; //, *audio_st;
	AVCodec *video_codec; //, *audio_codec;
	// stream members
	AVFrame *frame;
	AVPicture src_picture, dst_picture;
	int frame_count;
	int video_is_eof; //, audio_is_eof;

	// ffmpeg methods
	int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt);
	AVStream *add_stream(AVFormatContext *oc, AVCodec **codec, enum AVCodecID codec_id,
						int img_width, int img_height, int bit_rate);
	void open_video(AVFormatContext *oc, AVCodec *codec, AVStream *st);
	void write_video_frame(AVFormatContext *oc, AVStream *st, cv::Mat cv_img, int flush);
	void close_video(AVStream *st);

public:
	bool Initialize(int img_width, int img_height, int bit_rate, 
					enum AVCodecID codec_id = AV_CODEC_ID_MPEG4,
					std::string ip = "127.0.0.1", int port = 8554);
	void Deinitialize();
	bool StreamImage(cv::Mat cv_img, bool is_end);
	int GetLastError();
};

#endif