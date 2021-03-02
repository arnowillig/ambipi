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

DISPMANX_DISPLAY_HANDLE_T display;

FrameBuffer::FrameBuffer(const char* devicePath) : _devicePath(devicePath)
{
#if HAVE_DISPMANX
	bcm_host_init();
	display = vc_dispmanx_display_open(0);
#endif
	struct fb_var_screeninfo screen_info;
	int fd = -1;
	fd = open(devicePath, O_RDWR);
	if (fd >= 0) {
		if (!ioctl(fd, FBIOGET_VSCREENINFO, &screen_info)) {
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
	vc_dispmanx_display_close(display);
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

#if HAVE_DISPMANX

cv::Mat FrameBuffer::grabFrame() const
{
	int div = 8;
	int iw = 1920 / div; // info.width
	int ih = 1080 / div; // info.height
	cv::Mat frame = cv::Mat(ih, iw, CV_8UC3, cv::Scalar(64,64,64));

	// display = vc_dispmanx_display_open(0);

	// DISPMANX_MODEINFO_T info;
	// int ret = vc_dispmanx_display_get_info(display, &info);
	// printf("Display is %d x %d\n", info.width, info.height);
	
	int32_t dmxPitch = 3 * ALIGN_TO_16(iw);

	uint32_t vc_image_ptr;
	DISPMANX_RESOURCE_HANDLE_T resource = vc_dispmanx_resource_create(VC_IMAGE_RGB888, iw, ih, &vc_image_ptr);

	VC_RECT_T rect;
	vc_dispmanx_snapshot(display, resource, DISPMANX_NO_ROTATE);
	vc_dispmanx_rect_set(&rect, 0, 0, iw, ih);
	vc_dispmanx_resource_read_data(resource, &rect, frame.data, dmxPitch);

	vc_dispmanx_resource_delete(resource);
	// vc_dispmanx_display_close(display);
	
	// cv::cvtColor(frame, frame, cv::COLOR_RGB2BGR);

	int bx = 44 / div; // 290 (Sega)
	int by = 44 / div;
	return frame(cv::Rect(bx,by,iw-2*bx,ih-2*by)).clone();
}


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



int dispmanx(void)
{
	
	bcm_host_init();

	// DISPMANX_DISPLAY_HANDLE_T display = vc_dispmanx_display_open(0);

	DISPMANX_MODEINFO_T info;
	int ret = vc_dispmanx_display_get_info(display, &info);
	assert(ret == 0);
	printf("Display is %d x %d\n", info.width, info.height );

	void* image = calloc( 1, info.width * 3 * info.height);
	
	uint32_t vc_image_ptr;
	DISPMANX_RESOURCE_HANDLE_T resource = vc_dispmanx_resource_create(VC_IMAGE_RGB888, info.width, info.height, &vc_image_ptr);

	VC_RECT_T rect;
	DISPMANX_TRANSFORM_T transform;
	vc_dispmanx_snapshot(display, resource, transform);
	vc_dispmanx_rect_set(&rect, 0, 0, info.width, info.height);
	vc_dispmanx_resource_read_data(resource, &rect, image, info.width*3);

	FILE *fp = fopen("out.ppm", "wb");
	fprintf(fp, "P6\n%d %d\n255\n", info.width, info.height);
	fwrite(image, info.width*3*info.height, 1, fp);
	fclose(fp);

	ret = vc_dispmanx_resource_delete(resource);
	assert( ret == 0 );
//	ret = vc_dispmanx_display_close(display);
	assert( ret == 0 );

	return 0;
}
#endif




#ifndef ALIGN_UP
#define ALIGN_UP(x,y) ((x + (y)-1) & ~((y)-1))
#endif
/*



class Rect {
    static int rectCount;
//	DISPMANX_DISPLAY_HANDLE_T display = vc_dispmanx_display_open(0);
    static DISPMANX_DISPLAY_HANDLE_T   display;
    static DISPMANX_MODEINFO_T         info;
    static DISPMANX_RESOURCE_HANDLE_T  resource;
    void                              *image;
    static DISPMANX_UPDATE_HANDLE_T    update;
    DISPMANX_ELEMENT_HANDLE_T          element;
    uint32_t                           vc_image_ptr;
    VC_RECT_T       src_rect;
    VC_RECT_T       dst_rect;
    VC_DISPMANX_ALPHA_T alpha;
    int x, y;
    int width, height;
    int pitch;
    int aligned_height;
    VC_IMAGE_TYPE_T type;

public:
    Rect(int x, int y, int width, int height, unsigned char* data):x(x),y(y),width(width),height(height) {
	if(display == 0) {
	    bcm_host_init();
	    display = vc_dispmanx_display_open(5);
	    int ret = vc_dispmanx_display_get_info(display, &info);
	    assert(ret == 0);
	    std::cout << "Display:  " << info.width << " x " << info.height << std::endl;
	}
//        pitch = ALIGN_UP(width*2, 32);
	pitch = ALIGN_UP(width*3, 32);
	aligned_height = ALIGN_UP(height, 16);
//        type = VC_IMAGE_BGRX8888;
	type = VC_IMAGE_BGR888;
//        image = calloc(1, pitch * height);

	image=data;

	assert(image);

   }

    int getCount() { return rectCount; }

    void beforeDraw() {
	resource = vc_dispmanx_resource_create(type,
					       width,
					       height,
					       &vc_image_ptr);
	assert(resource);
	update = vc_dispmanx_update_start(0);
	assert(update);
    }

    void draw(int layer) {
	VC_DISPMANX_ALPHA_T alpha = {
			DISPMANX_FLAGS_ALPHA_FROM_SOURCE,
			120, / *alpha 0->255* /
			0
	};

	vc_dispmanx_rect_set(&dst_rect, 0, 0, width, height);
	int ret = vc_dispmanx_resource_write_data(resource,
		      type,
		      pitch,
		      image,
		      &dst_rect);
	assert(ret == 0);

	vc_dispmanx_rect_set(&src_rect, 0, 0, width << 16, height << 16);

	vc_dispmanx_rect_set(&dst_rect, x, y, width, height);

	element = vc_dispmanx_element_add(update,
		      display,
		      layer,        // layer
		      &dst_rect,
		      resource,
		      &src_rect,
		      DISPMANX_PROTECTION_NONE,
		      &alpha,
		      NULL,        // clamp
		      DISPMANX_NO_ROTATE);
    }

    void afterDraw() {
	int ret = vc_dispmanx_update_submit_sync(update);
	assert(ret == 0);
    }

    ~Rect() {
	update = vc_dispmanx_update_start(0);
	assert(update);
	int ret = vc_dispmanx_element_remove(update, element);
	assert(ret == 0);
	ret = vc_dispmanx_update_submit_sync(update);
	assert(ret == 0);
	rectCount--;
	if(rectCount == 0) {
	    std::cout << "No more rectangles" << std::endl;
	    ret = vc_dispmanx_resource_delete(resource);
	    assert(ret == 0);
	    ret = vc_dispmanx_display_close(display);
	    assert(ret == 0);
	    display = 0;
	}
    }

};

DISPMANX_DISPLAY_HANDLE_T Rect::display;
int Rect::rectCount = 0;
DISPMANX_MODEINFO_T Rect::info;
DISPMANX_RESOURCE_HANDLE_T Rect::resource;
DISPMANX_UPDATE_HANDLE_T Rect::update;
*/
