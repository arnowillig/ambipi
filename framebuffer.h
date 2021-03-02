#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <mutex>
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
	mutable std::mutex _mutex;
#if HAVE_DISPMANX
	DISPMANX_DISPLAY_HANDLE_T _display;
#endif
public:
	FrameBuffer(const char* devicePath);
	~FrameBuffer();
	void clear();
	void drawFrame(cv::Mat frame);
	cv::Mat grabFrame(int div=8, bool rgb=false) const;
};

#endif // FRAMEBUFFER_H
