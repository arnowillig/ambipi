#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <opencv2/opencv.hpp>

#if HAVE_DISPMANX
#include <bcm_host.h>
#endif


class FrameBuffer
{
	uint32_t _bits_per_pixel;
	uint32_t _xres_virtual;
	uint32_t _yres_virtual;
	const char* _devicePath;
#if HAVE_DISPMANX
	DISPMANX_DISPLAY_HANDLE_T _display;
#endif
public:
	FrameBuffer(const char* devicePath);
	~FrameBuffer();
	void clear();
	void drawFrame(cv::Mat frame);
#if HAVE_DISPMANX
	cv::Mat grabFrame(int div=8, bool rgb=false) const;
#endif
};

#endif // FRAMEBUFFER_H
