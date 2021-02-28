#include "framebuffer.h"
#include <algorithm>

#include <iostream> // for std::cerr
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <fstream>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <fcntl.h>


FrameBuffer::FrameBuffer(const char* devicePath) : _devicePath(devicePath)
{
	struct fb_var_screeninfo screen_info;
	int fd = -1;
	fd = open(devicePath, O_RDWR);
	if (fd >= 0) {
		if (!ioctl(fd, FBIOGET_VSCREENINFO, &screen_info)) {
			_xres_virtual = screen_info.xres_virtual;
			_bits_per_pixel = screen_info.bits_per_pixel;
		}
	}
}

FrameBuffer::~FrameBuffer()
{
	clear();
}

void FrameBuffer::clear()
{
	cv::Mat frame = cv::Mat(1080, 1920, CV_8UC3, cv::Scalar(0,0,0));
	drawFrame(frame);
}

void FrameBuffer::drawFrame(cv::Mat frame)
{
	if (frame.empty()) {
		return;
	}
	std::ofstream ofs(_devicePath);
	if (frame.depth() != CV_8U) {
		std::cerr << "Not 8 bits per pixel and channel." << std::endl;
	} else if (frame.channels() != 3) {
		std::cerr << "Not 3 channels." << std::endl;
	} else {
		// 3 Channels (assumed BGR), 8 Bit per Pixel and Channel
		cv::Size2f frame_size = frame.size();
		cv::Mat framebuffer_compat;
		switch (_bits_per_pixel) {
		case 16:
			cv::cvtColor(frame, framebuffer_compat, cv::COLOR_BGR2BGR565);
			for (int y = 0; y < frame_size.height ; y++) {
				ofs.seekp(y*_xres_virtual*2);
				ofs.write(reinterpret_cast<char*>(framebuffer_compat.ptr(y)),frame_size.width*2);
			}
			break;
		case 32: {
			std::vector<cv::Mat> split_bgr;
			cv::split(frame, split_bgr);
			split_bgr.push_back(cv::Mat(frame_size,CV_8UC1,cv::Scalar(255)));
			cv::merge(split_bgr, framebuffer_compat);
			for (int y = 0; y < frame_size.height ; y++) {
				ofs.seekp(y*_xres_virtual*4);
				ofs.write(reinterpret_cast<char*>(framebuffer_compat.ptr(y)),frame_size.width*4);
			}
		} break;
		default:
			std::cerr << "Unsupported depth of framebuffer." << std::endl;
		}
	}
}
