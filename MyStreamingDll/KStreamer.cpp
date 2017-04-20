#include <iostream>
#include "KStreamer.h"

KStreamer::KStreamer()
	: is_streaming(false), last_error(KStreamerError::NO_STREAMER_ERROR), 
	sender(NULL), device_id(0), video_cap(), zed_camera(NULL), zed_params(), 
	ffmpeg(), sendEvent(NULL)
{}

KStreamer::~KStreamer()
{}

void KStreamer::SetFFMPEG(int img_width, int img_height, int64_t bit_rate, 
						enum AVCodecID codec_id, std::string ip, int port)
{
	ffmpeg.Deinitialize();
	ffmpeg.Initialize(img_width, img_height, bit_rate, codec_id, ip, port);
}

void KStreamer::SetCamDeviceID(int id)
{
	this->device_id = id;
}

bool KStreamer::StartStream()
{
	EndStream();

	if (this->device_id == DEVICE_OPTION::ZED_CAMERA_LEFT ||
		this->device_id == DEVICE_OPTION::ZED_CAMERA_RIGHT ||
		this->device_id == DEVICE_OPTION::ZED_CAMERA_STEREO)
	{
		this->zed_camera = new sl::zed::Camera(sl::zed::HD720, 30);
		this->zed_params.mode = sl::zed::PERFORMANCE;
		this->zed_params.unit = sl::zed::MILLIMETER;
		this->zed_params.coordinate = sl::zed::IMAGE;
		this->zed_params.disableSelfCalib = false;
		this->zed_params.device = -1;
		this->zed_params.verbose = false;
		this->zed_params.vflip = false;

		sl::zed::ERRCODE zederr = zed_camera->init(zed_params);

		if (zederr != sl::zed::SUCCESS)
		{
			this->last_error = KStreamerError::CAM_NOT_OPENED;
			delete zed_camera;
			return false;
		}
	}
	else
	{
		this->video_cap.open(this->device_id);
		if (!this->video_cap.isOpened())
		{
			this->last_error = KStreamerError::CAM_NOT_OPENED;
			return false;
		}
	}

	mtx_lock.lock();
	this->is_streaming = true;
	mtx_lock.unlock();

	this->sender = new std::thread(&KStreamer::SendStream, this);
	if (!this->sender)
	{
		this->last_error = KStreamerError::THREAD_NOT_CREATED;
		return false;
	}

	return true;
}

void KStreamer::EndStream()
{
	if (this->sender)
	{
		mtx_lock.lock();
		this->is_streaming = false;
		mtx_lock.unlock();

		// wait until finish
		this->sender->join();

		delete this->sender;
	}
	if (this->video_cap.isOpened())
		this->video_cap.release();
	if (this->zed_camera)
	{
		delete this->zed_camera;
		this->zed_camera = NULL;
	}

	this->sender = NULL;
}

int KStreamer::GetLastError()
{
	if (KStreamerError::FFMPEG_ERROR)
		return this->ffmpeg.GetLastError();
	else
		return this->last_error;
}

void KStreamer::SetSendEvent(void(*sendEvent)(__in cv::Mat& cv_img))
{
	this->sendEvent = sendEvent;
}

void KStreamer::SendStream()
{
	cv::Mat cam_img;
	cv::Mat frame_pool[STREAM_FPS];
	int frame_pool_index = 0;
	int func_device_id = this->device_id;

	while (true)
	{
		mtx_lock.lock();
		bool thread_end = this->is_streaming;
		mtx_lock.unlock();

		// get image from camera
		if (func_device_id == DEVICE_OPTION::ZED_CAMERA_LEFT ||
			func_device_id == DEVICE_OPTION::ZED_CAMERA_RIGHT)
		{
			if (!zed_camera)
			{
				this->last_error = KStreamerError::CAM_NOT_OPENED;
				return;
			}

			int width = zed_camera->getImageSize().width;
			int height = zed_camera->getImageSize().height;
			cv::Mat cam_temp = cv::Mat(height, width, CV_8UC4);
			cam_img = cv::Mat(height, width, CV_8UC3);

			if (!zed_camera->grab(sl::zed::SENSING_MODE::STANDARD))
			{
				// Retrieve left color image
				sl::zed::Mat zedMat;
				if (func_device_id == DEVICE_OPTION::ZED_CAMERA_LEFT)
					zedMat = zed_camera->retrieveImage(sl::zed::SIDE::LEFT);
				else if (func_device_id == DEVICE_OPTION::ZED_CAMERA_RIGHT)
					zedMat = zed_camera->retrieveImage(sl::zed::SIDE::RIGHT);

				memcpy(cam_temp.data, zedMat.data, width*height * 4 * sizeof(uchar));

				cv::cvtColor(cam_temp, cam_img, cv::COLOR_BGRA2BGR);
			}
		}
		else if (func_device_id == DEVICE_OPTION::ZED_CAMERA_STEREO)
		{
			if (!zed_camera)
			{
				this->last_error = KStreamerError::CAM_NOT_OPENED;
				return;
			}

			int width = zed_camera->getImageSize().width;
			int height = zed_camera->getImageSize().height;
			cv::Mat cam_temp = cv::Mat(height, width * 2, CV_8UC4);
			cam_img = cv::Mat(height, width * 2, CV_8UC3);
			cv::Mat zed_left = cv::Mat(height, width, CV_8UC4);
			cv::Mat zed_right = cv::Mat(height, width, CV_8UC4);

			if (!zed_camera->grab(sl::zed::SENSING_MODE::STANDARD))
			{
				// Retrieve left color image
				sl::zed::Mat zedMat, zedMat2;
				zedMat = zed_camera->retrieveImage(sl::zed::SIDE::LEFT);
				memcpy(zed_left.data, zedMat.data, width * height * 4 * sizeof(uchar));
				zedMat2 = zed_camera->retrieveImage(sl::zed::SIDE::RIGHT);
				memcpy(zed_right.data, zedMat2.data, width * height * 4 * sizeof(uchar));//width*height * 4 * sizeof(uchar));
				cv::Mat zed_roi = cam_temp(cv::Range(0, height), cv::Range(0, width));
				zed_left.copyTo(zed_roi);
				zed_roi = cam_temp(cv::Range(0, height), cv::Range(width, 2*width));
				zed_right.copyTo(zed_roi);

				cv::cvtColor(cam_temp, cam_img, cv::COLOR_BGRA2BGR);
			}
		}
		else
			this->video_cap >> cam_img;

		// end of video stream
		if (cam_img.empty())
		{
			EndStream();
			break;
		}

		// user finish
		if (!thread_end)
		{
			// write last frame
			if (!this->ffmpeg.StreamImage(cam_img, true))
				this->last_error = KStreamerError::FFMPEG_ERROR;
			break;
		}
		
		// write frame
		if (!this->ffmpeg.StreamImage(cam_img, false))
			this->last_error = KStreamerError::FFMPEG_ERROR;

		// occur event
		if (this->sendEvent != NULL)
		{
			frame_pool[frame_pool_index] = cam_img.clone();
			this->sendEvent(frame_pool[frame_pool_index]);
			frame_pool_index = (frame_pool_index + 1) % STREAM_FPS;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1000 / STREAM_FPS));
	}
}