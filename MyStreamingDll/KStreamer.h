#ifndef _K_STREAMER_H_
#define _K_STREAMER_H_

#ifdef KSTREAMINGDLL_EXPORTS
#define K_STREAMING_API __declspec(dllexport)
#else
#define K_STREAMING_API __declspec(dllimport)
#endif

#include <mutex>
#include <thread>

extern "C"
{
#include <libavcodec/avcodec.h>
}

#include <opencv2/core/core.hpp> // Basic OpenCV structures (cv::Mat)
#include <opencv2/highgui/highgui.hpp>

#include "MyFFMPEGStreamer.h"

// opencv
#pragma comment(lib, "opencv_core2413.lib")
#pragma comment(lib, "opencv_core2413d.lib")
#pragma comment(lib, "opencv_highgui2413.lib")
#pragma comment(lib, "opencv_highgui2413d.lib")

enum KStreamerError{
	CAM_NOT_OPENED = 0,
	THREAD_NOT_CREATED = 1,
	FFMPEG_ERROR = 9, 
	NO_STREAMER_ERROR = 100
};

class K_STREAMING_API KStreamer
{
public:
	KStreamer();
	~KStreamer();

private:
	bool is_streaming;
	enum KStreamerError last_error;
	// send stream from camera device to remote media server
	std::mutex mtx_lock;
	std::thread* sender;
	// opencv for capture
	int device_id;
	cv::VideoCapture video_cap;
	// ffmpeg members
	MyFFMPEGStreamer ffmpeg;
	// stream sender
	void SendStream();

public:
	void SetFFMPEG(int img_width, int img_height,
				enum AVCodecID codec_id = AV_CODEC_ID_MPEG4, 
				std::string ip = "127.0.0.1", int port = 8554);
	void SetCamDeviceID(int id);
	bool StartStream();
	void EndStream();
	int GetLastError();
};

#endif