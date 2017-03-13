#include <iostream>
#include "KStreamer.h"

KStreamer::KStreamer()
	: is_streaming(false), last_error(KStreamerError::NO_STREAMER_ERROR), 
	sender(NULL), device_id(0), video_cap(), ffmpeg()
{}

KStreamer::~KStreamer()
{}

void KStreamer::SetFFMPEG(int img_width, int img_height,
						enum AVCodecID codec_id, std::string ip, int port)
{
	ffmpeg.Deinitialize();
	ffmpeg.Initialize(img_width, img_height, codec_id, ip, port);
}

void KStreamer::SetCamDeviceID(int id)
{
	this->device_id = id;
}

bool KStreamer::StartStream()
{
	EndStream();

	this->video_cap.open(this->device_id);
	if (!this->video_cap.isOpened())
	{
		this->last_error = KStreamerError::CAM_NOT_OPENED;
		return false;
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

	this->sender = NULL;
}

int KStreamer::GetLastError()
{
	if (KStreamerError::FFMPEG_ERROR)
		return this->ffmpeg.GetLastError();
	else
		return this->last_error;
}

void KStreamer::SendStream()
{
	cv::Mat cam_img;

	while (true)
	{
		mtx_lock.lock();
		bool thread_end = this->is_streaming;
		mtx_lock.unlock();

		// get image from opencv
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
		if(!this->ffmpeg.StreamImage(cam_img, false))
			this->last_error = KStreamerError::FFMPEG_ERROR;

		std::this_thread::sleep_for(std::chrono::milliseconds(1000 / STREAM_FPS));
	}
}