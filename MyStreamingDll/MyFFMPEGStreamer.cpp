#include <iostream>
#include "MyFFMPEGStreamer.h"

MyFFMPEGStreamer::MyFFMPEGStreamer()
	: last_error(MyFFMPEGStreamerError::NO_FFMPEG_ERROR), 
	ip("127.0.0.1"), port(8554), codec_id(AV_CODEC_ID_MPEG4),
	fmt(NULL), oc(NULL), video_st(NULL), video_is_eof(0) //, audio_st(NULL), audio_is_eof(0)
{}

MyFFMPEGStreamer::~MyFFMPEGStreamer()
{
	Deinitialize();
}

bool MyFFMPEGStreamer::Initialize(int img_width, int img_height,
						enum AVCodecID codec_id, std::string ip, int port)
{
	int ret;

	/* Initialize libavcodec, and register all codecs and formats. */
	av_register_all();
	avformat_network_init();

	/* allocate the output media context */
	std::string tempUrl("");
	tempUrl.append("rtp://");
	tempUrl.append(ip + ":");
	tempUrl.append(std::to_string(port));
	//tempUrl.append("/live.sdp");
	tempUrl.append("/kstream");
	avformat_alloc_output_context2(&this->oc, NULL, "rtp", tempUrl.c_str());
	if (!this->oc)
	{
		this->last_error = MyFFMPEGStreamerError::CANT_ALLOC_FORMAT_CONTEXT;
		return false;
	}

	// alloc output format
	this->fmt = oc->oformat;
	if (!this->fmt)
	{
		this->last_error = MyFFMPEGStreamerError::CANT_ALLOC_OUTPUT_FORMAT;
		return false;
	}

	// set codec
	this->fmt->video_codec = codec_id;

	if (fmt->video_codec != AV_CODEC_ID_NONE)
		this->video_st = add_stream(oc, &video_codec, fmt->video_codec, img_width, img_height);

	/* Now that all the parameters are set, we can open the audio and
	* video codecs and allocate the necessary encode buffers. */
	if (this->video_st)
		open_video(this->oc, this->video_codec, this->video_st);

	av_dump_format(this->oc, 0, tempUrl.c_str(), 1);
	char errorBuff[80];

	if (!(fmt->flags & AVFMT_NOFILE)) {
		ret = avio_open(&oc->pb, tempUrl.c_str(), AVIO_FLAG_WRITE);
		if (ret < 0) {
			this->last_error = MyFFMPEGStreamerError::CANT_OPEN_RTSP_OUTPUT;
			fprintf(stderr, "Could not open outfile '%s': %s", tempUrl.c_str(), av_make_error_string(errorBuff, 80, ret));
			return false;
		}
	}

	ret = avformat_write_header(oc, NULL);
	if (ret < 0) {
		this->last_error = MyFFMPEGStreamerError::CANT_WRITE_HEADER;
		fprintf(stderr, "Error occurred when writing header: %s", av_make_error_string(errorBuff, 80, ret));
		return false;
	}

	return true;
}

void MyFFMPEGStreamer::Deinitialize()
{
	/* Write the trailer, if any. The trailer must be written before you
	* close the CodecContexts open when you wrote the header; otherwise
	* av_write_trailer() may try to use memory that was freed on
	* av_codec_close(). */
	if (this->oc)
		av_write_trailer(this->oc);

	/* Close each codec. */
	if (this->video_st)
		close_video(this->video_st);
	//if (audio_st)
	//	close_audio(this->audio_st);

	if (this->fmt && this->oc)
		if (!(this->fmt->flags & AVFMT_NOFILE))
			/* Close the output file. */
			avio_close(this->oc->pb);

	/* free the stream */
	if (this->oc)
		avformat_free_context(this->oc);

	this->oc = NULL;
	this->fmt = NULL;
	this->video_st = NULL;
}

bool MyFFMPEGStreamer::StreamImage(cv::Mat cv_img, bool is_end)
{
	if (this->video_st && !this->video_is_eof)
	{
		write_video_frame(this->oc, this->video_st, cv_img, is_end);
		return true;
	}
	else
		return false;
}

int MyFFMPEGStreamer::GetLastError()
{
	return this->last_error;
}

// ffmpeg methods
int MyFFMPEGStreamer::write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
	/* rescale output packet timestamp values from codec to stream timebase */
	pkt->pts = av_rescale_q_rnd(pkt->pts, *time_base, st->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	pkt->dts = av_rescale_q_rnd(pkt->dts, *time_base, st->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	pkt->duration = av_rescale_q(pkt->duration, *time_base, st->time_base);
	pkt->stream_index = st->index;

	/* Write the compressed frame to the media file. */
	return av_interleaved_write_frame(fmt_ctx, pkt);
}

AVStream* MyFFMPEGStreamer::add_stream(AVFormatContext *oc, AVCodec **codec, enum AVCodecID codec_id,
							int img_width, int img_height)
{
	AVCodecContext *c;
	AVStream *st;

	/* find the encoder */
	*codec = avcodec_find_encoder(codec_id);
	if (!(*codec)) {
		fprintf(stderr, "Could not find encoder for '%s'\n",
			avcodec_get_name(codec_id));
		exit(1);
	}

	st = avformat_new_stream(oc, *codec);
	if (!st) {
		fprintf(stderr, "Could not allocate stream\n");
		exit(1);
	}
	st->id = oc->nb_streams - 1;
	c = st->codec;

	switch ((*codec)->type) {
	case AVMEDIA_TYPE_AUDIO:
		c->sample_fmt = (*codec)->sample_fmts ?
			(*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
		c->bit_rate = 64000;
		c->sample_rate = 44100;
		c->channels = 2;
		break;

	case AVMEDIA_TYPE_VIDEO:
		c->codec_id = codec_id;
		c->bit_rate = 3500000;
		/* Resolution must be a multiple of two. */
		c->width = img_width;
		c->height = img_height;
		/* timebase: This is the fundamental unit of time (in seconds) in terms
		* of which frame timestamps are represented. For fixed-fps content,
		* timebase should be 1/framerate and timestamp increments should be
		* identical to 1. */
		c->time_base.den = STREAM_FPS;
		c->time_base.num = 1;
		c->gop_size = 12; /* emit one intra frame every twelve frames at most */
		c->pix_fmt = STREAM_PIX_FMT;
		if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
			/* just for testing, we also add B frames */
			c->max_b_frames = 2;
		}
		if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
			/* Needed to avoid using macroblocks in which some coeffs overflow.
			* This does not happen with normal video, it just happens here as
			* the motion of the chroma plane does not match the luma plane. */
			c->mb_decision = 2;
		}
		break;

	default:
		break;
	}

	/* Some formats want stream headers to be separate. */
	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;

	return st;
}

void MyFFMPEGStreamer::open_video(AVFormatContext *oc, AVCodec *codec, AVStream *st)
{
	int ret;
	AVCodecContext *c = st->codec;

	/* open the codec */
	ret = avcodec_open2(c, codec, NULL);
	if (ret < 0) {
		fprintf(stderr, "Could not open video codec: ");
		exit(1);
	}

	/* allocate and init a re-usable frame */
	this->frame = av_frame_alloc();
	if (!this->frame) {
		fprintf(stderr, "Could not allocate video frame\n");
		exit(1);
	}
	this->frame->format = c->pix_fmt;
	this->frame->width = c->width;
	this->frame->height = c->height;

	/* Allocate the encoded raw picture. */
	ret = avpicture_alloc(&this->dst_picture, c->pix_fmt, c->width, c->height);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate picture: ");
		exit(1);
	}
	ret = avpicture_alloc(&this->src_picture, AV_PIX_FMT_BGR24, c->width, c->height);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate temporary picture:");
		exit(1);
	}

	/* copy data and linesize picture pointers to frame */
	*((AVPicture *)(this->frame)) = dst_picture;
}

void MyFFMPEGStreamer::write_video_frame(AVFormatContext *oc, AVStream *st, cv::Mat cv_img, int flush)
{
	int ret;
	static struct SwsContext *sws_ctx;
	AVCodecContext *c = st->codec;

	if (!flush) {
		// BGR opencv image to AV_PIX_FMT_YUV420P
		cv::resize(cv_img, cv_img, cv::Size(c->width, c->height));

		if (!sws_ctx) {
			sws_ctx = sws_getContext(c->width, c->height, AV_PIX_FMT_BGR24,
				c->width, c->height, c->pix_fmt,
				SWS_BICUBIC, NULL, NULL, NULL);
			if (!sws_ctx) {
				fprintf(stderr,
					"Could not initialize the conversion context\n");
				exit(1);
			}
		}

		avpicture_fill(&this->src_picture, cv_img.data, AV_PIX_FMT_BGR24, c->width, c->height);

		sws_scale(sws_ctx,
			(const uint8_t * const *)(this->src_picture.data), this->src_picture.linesize,
			0, c->height, this->dst_picture.data, this->dst_picture.linesize);
	}

	if (oc->oformat->flags & AVFMT_RAWPICTURE && !flush) {
		/* Raw video case - directly store the picture in the packet */
		AVPacket pkt;
		av_init_packet(&pkt);

		pkt.flags |= AV_PKT_FLAG_KEY;
		pkt.stream_index = st->index;
		pkt.data = this->dst_picture.data[0];
		pkt.size = sizeof(AVPicture);

		ret = av_interleaved_write_frame(oc, &pkt);
	}
	else {
		AVPacket pkt = { 0 };
		int got_packet;
		av_init_packet(&pkt);

		/* encode the image */
		this->frame->pts = this->frame_count;
		ret = avcodec_encode_video2(c, &pkt, flush ? NULL : this->frame, &got_packet);
		if (ret < 0) {
			fprintf(stderr, "Error encoding video frame:");
			exit(1);
		}
		/* If size is zero, it means the image was buffered. */

		if (got_packet) {
			//cout<<"got Packet"<<endl;
			ret = write_frame(oc, &c->time_base, st, &pkt);
		}
		else {
			//cout<<"EOF\n";
			if (flush)
				this->video_is_eof = 1;
			ret = 0;
		}
	}

	if (ret < 0) {
		fprintf(stderr, "Error while writing video frame: ");
		exit(1);
	}
	this->frame_count++;
}

void MyFFMPEGStreamer::close_video(AVStream *st)
{
	avcodec_close(st->codec);
	av_free(this->src_picture.data[0]);
	av_free(this->dst_picture.data[0]);
	av_frame_free(&this->frame);
}