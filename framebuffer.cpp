#include "framebuffer.h"
#include <algorithm>

#include <iostream>
#include <opencv2/imgproc/imgproc.hpp>
#include <fstream>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <fcntl.h>

#ifndef ALIGN_TO_16
#define ALIGN_TO_16(x)  ((x + 15) & ~15)
#endif

FrameBuffer::FrameBuffer(const char* devicePath) : _devicePath(devicePath)
{
#if HAVE_DISPMANX
	bcm_host_init();
	_display = vc_dispmanx_display_open(0);
#endif
	struct fb_var_screeninfo screen_info;
	int fd = -1;
	fd = open(devicePath, O_RDWR);
	if (fd >= 0) {
		if (0 == ioctl(fd, FBIOGET_VSCREENINFO, &screen_info)) {
			_xres_virtual = screen_info.xres_virtual;
			_yres_virtual = screen_info.yres_virtual;
			_bits_per_pixel = screen_info.bits_per_pixel;
		}
	}
}

FrameBuffer::~FrameBuffer()
{
	clear();
#if HAVE_DISPMANX
	vc_dispmanx_display_close(_display);
#endif
}

void FrameBuffer::clear()
{
	drawFrame(cv::Mat(_yres_virtual, _xres_virtual, CV_8UC3, cv::Scalar(0,0,0)));
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

cv::Mat FrameBuffer::grabFrame(int div, bool rgb) const
{
	// int div = 8;
	int iw = 1920 / div; // info.width
	int ih = 1080 / div; // info.height
	cv::Mat frame = cv::Mat(ih, iw, CV_8UC3, cv::Scalar(64,64,64));

#if HAVE_DISPMANX
	_mutex.lock();
	int32_t dmxPitch = 3 * ALIGN_TO_16(iw);
	uint32_t vc_image_ptr;
	VC_RECT_T rect;
	DISPMANX_RESOURCE_HANDLE_T resource = vc_dispmanx_resource_create(VC_IMAGE_RGB888, iw, ih, &vc_image_ptr);
	vc_dispmanx_snapshot(_display, resource, DISPMANX_NO_ROTATE);
	vc_dispmanx_rect_set(&rect, 0, 0, iw, ih);
	vc_dispmanx_resource_read_data(resource, &rect, frame.data, dmxPitch);
	vc_dispmanx_resource_delete(resource);
	_mutex.unlock();
#endif

	if (rgb) {
		cv::cvtColor(frame, frame, cv::COLOR_RGB2BGR);
	}

	int bx = 44 / div; // 290 (Sega)
	int by = 44 / div;
	return frame(cv::Rect(bx,by,iw-2*bx,ih-2*by)).clone();
}

#if HAVE_DISPMANX
void drawToDispManX(cv::Mat frame)
{
	static bool initBCM = false;
	if (!initBCM) {
		bcm_host_init();
		initBCM = true;
	}

	DISPMANX_DISPLAY_HANDLE_T display = vc_dispmanx_display_open(0);
	DISPMANX_MODEINFO_T info;
	int ret = vc_dispmanx_display_get_info(display, &info);
	assert(ret == 0);
	printf("Display is %d x %d\n", info.width, info.height );

	uint32_t vc_image_ptr;
	DISPMANX_RESOURCE_HANDLE_T resource = vc_dispmanx_resource_create(VC_IMAGE_RGB888, info.width, info.height, &vc_image_ptr);

	VC_RECT_T rect;
	vc_dispmanx_rect_set(&rect, 0, 0, info.width, info.height);

	int  pitch = frame.cols*3; // ALIGN_UP(width*3, 32);

	DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);

	VC_RECT_T dst_rect;
	ret = vc_dispmanx_resource_write_data(resource, VC_IMAGE_RGB888, pitch, frame.data, &dst_rect);
	assert(ret == 0);

	int layer = 3;

	VC_DISPMANX_ALPHA_T alpha;

	DISPMANX_ELEMENT_HANDLE_T element = vc_dispmanx_element_add(update,
								    display,
								    layer,        // layer
								    &dst_rect,
								    resource,
								    &rect,
								    DISPMANX_PROTECTION_NONE,
								    &alpha,
								    NULL,        // clamp
								    DISPMANX_NO_ROTATE);


	ret = vc_dispmanx_update_submit_sync(update);
	assert(ret == 0);


	usleep(500*1000);

	update = vc_dispmanx_update_start(0);
	assert(update);
	ret = vc_dispmanx_element_remove(update, element);
	assert(ret == 0);
	ret = vc_dispmanx_update_submit_sync(update);
	assert(ret == 0);


	ret = vc_dispmanx_display_close(display);
	assert( ret == 0 );
}

#endif
