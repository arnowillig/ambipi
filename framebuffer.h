#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <opencv2/opencv.hpp>


class FrameBuffer
{
	uint32_t _bits_per_pixel;
	uint32_t _xres_virtual;
	const char* _devicePath;
	//	framebuffer_info fb_info = get_framebuffer_info("/dev/fb0");
	//	std::ofstream ofs("/dev/fb0");

public:
	FrameBuffer(const char* devicePath);
	~FrameBuffer();
	void clear();
	void drawFrame(cv::Mat frame);
};

#endif // FRAMEBUFFER_H
