#ifndef _K_STREAMER_H_
#define _K_STREAMER_H_

#ifdef KSTREAMINGDLL_EXPORTS
#define K_STREAMING_API __declspec(dllexport)
#define K_STREAMING_ZED
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
#include <opencv2/imgproc/imgproc.hpp>

#ifdef K_STREAMING_ZED
#include <zed/Camera.hpp>
#endif

#include "MyFFMPEGStreamer.h"

// opencv
#pragma comment(lib, "opencv_core2413.lib")
#pragma comment(lib, "opencv_core2413d.lib")
#pragma comment(lib, "opencv_highgui2413.lib")
#pragma comment(lib, "opencv_highgui2413d.lib")
#pragma comment(lib, "opencv_imgproc2413.lib")
#pragma comment(lib, "opencv_imgproc2413d.lib")

// zed camera
#ifdef K_STREAMING_ZED
#pragma comment(lib, "sl_zed64.lib")
#endif

enum KStreamerError{
	CAM_NOT_OPENED = 0,
	THREAD_NOT_CREATED = 1,
	FFMPEG_ERROR = 9, 
	NO_STREAMER_ERROR = 100
};

enum DEVICE_OPTION{
	ZED_CAMERA_LEFT = 100, 
	ZED_CAMERA_RIGHT = 101,
	ZED_CAMERA_STEREO = 102, 
	ALL_ROUND_CAMERA = 110,
	MANUAL = 120
};

class K_STREAMING_API KStreamer
{
public:
	KStreamer();
#ifdef K_STREAMING_ZED
	KStreamer(__in sl::zed::Camera* zed_camera, __in const sl::zed::InitParams& zed_params);
#endif
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
	// zed camera
#ifdef K_STREAMING_ZED
	bool is_zed_outside;
	sl::zed::Camera* zed_camera;
	sl::zed::InitParams zed_params;
#endif
	// ffmpeg members
	MyFFMPEGStreamer ffmpeg;
	// stream sender
	void SendStream();

	// event occur when send image successfully
	void(*sendEvent)(__in cv::Mat& cv_img);

public:
	void SetFFMPEG(int img_width, int img_height, int64_t bit_rate, 
				enum AVCodecID codec_id = AV_CODEC_ID_MPEG4, 
				std::string ip = "127.0.0.1", int port = 8554);
	void SetCamDeviceID(int id);
	bool StartStream();
	void EndStream();
	bool SendFrameManually(__in const cv::Mat& cv_img);
	int GetLastError();
	/*
	set event to get image when streamer succesfully send.
	*/
	void SetSendEvent(void(*sendEvent)(__in cv::Mat& cv_img));
};

#endif