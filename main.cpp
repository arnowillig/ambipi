#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include <algorithm>

#include <pistache/http.h>
#include <pistache/router.h>
#include <pistache/endpoint.h>

#include <thread>

#include "ambipi.h"
#include "restserver.h"

using namespace std;
using namespace Pistache;


static bool running = true;
static bool screenshot = true;

static void signalHandler(int signo)
{
	switch (signo) {
	case SIGINT:
	case SIGTERM:
		running = false;
		break;
	case SIGUSR1:
		screenshot = true;
		break;
	default:
		break;

	}
}

#include <iostream> // for std::cerr
#include <opencv2/imgproc/imgproc.hpp>  // for cv::cvtColor
#include <opencv2/highgui/highgui.hpp> // for cv::VideoCapture
#include <fstream> // for std::ofstream
#include <boost/timer/timer.hpp> // for boost::timer::cpu_timer

// this is C :/
#include <stdint.h> // for uint32_t
#include <sys/ioctl.h> // for ioctl
#include <linux/fb.h> // for fb_
#include <fcntl.h> // for O_RDWR

struct framebuffer_info { 
    uint32_t bits_per_pixel; uint32_t xres_virtual; 
};
struct framebuffer_info get_framebuffer_info(const char* framebuffer_device_path) {
    struct framebuffer_info info;
    struct fb_var_screeninfo screen_info;
    int fd = -1;
    fd = open(framebuffer_device_path, O_RDWR);
    if (fd >= 0) {
        if (!ioctl(fd, FBIOGET_VSCREENINFO, &screen_info)) {
            info.xres_virtual = screen_info.xres_virtual;
            info.bits_per_pixel = screen_info.bits_per_pixel;
        }
    }
    return info;
};


static void restServer(RESTServer* restServer)
{
	restServer->start(9080);
}


void drawToFrameBuffer(struct framebuffer_info* fb_info, cv::Mat frame)
{
	if (frame.empty()) {
		return;
	}
	std::ofstream ofs("/dev/fb0");
            if (frame.depth() != CV_8U) {
                std::cerr << "Not 8 bits per pixel and channel." << std::endl;
            } else if (frame.channels() != 3) {
                std::cerr << "Not 3 channels." << std::endl;
            } else {
                // 3 Channels (assumed BGR), 8 Bit per Pixel and Channel
                int framebuffer_width = fb_info->xres_virtual;
                int framebuffer_depth = fb_info->bits_per_pixel;
                cv::Size2f frame_size = frame.size();
                cv::Mat framebuffer_compat;
                switch (framebuffer_depth) {
                    case 16:
                        cv::cvtColor(frame, framebuffer_compat, cv::COLOR_BGR2BGR565);
                        for (int y = 0; y < frame_size.height ; y++) {
                            ofs.seekp(y*framebuffer_width*2);
                            ofs.write(reinterpret_cast<char*>(framebuffer_compat.ptr(y)),frame_size.width*2);
                        }
                    break;
                    case 32: {
                            std::vector<cv::Mat> split_bgr;
                            cv::split(frame, split_bgr);
                            split_bgr.push_back(cv::Mat(frame_size,CV_8UC1,cv::Scalar(255)));
                            cv::merge(split_bgr, framebuffer_compat);
                            for (int y = 0; y < frame_size.height ; y++) {
                                ofs.seekp(y*framebuffer_width*4);
                                ofs.write(reinterpret_cast<char*>(framebuffer_compat.ptr(y)),frame_size.width*4);
                            }
                        } break;
                    default:
                        std::cerr << "Unsupported depth of framebuffer." << std::endl;
                }
            }
}

int main(int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	signal(SIGINT,  signalHandler);
	signal(SIGTERM, signalHandler);
	signal(SIGUSR1, signalHandler);
	
	fprintf(stderr, "AmbiPi\n");
	
	
	framebuffer_info fb_info = get_framebuffer_info("/dev/fb0");
	std::ofstream ofs("/dev/fb0");
	
	AmbiPi ambiPi;
	if (!ambiPi.init(0)) {
		return 1;
	}

	RESTServer server(&ambiPi);
	std::thread restThread(&restServer, &server);

#if 1
	cv::Mat frame;
	fprintf(stderr, "Setting color..\n");
	// ambiPi.setMode(AmbiPi::White);
	ambiPi.setMode(AmbiPi::AmbiLight);
	
	cv::VideoCapture* capture = new cv::VideoCapture("/home/pi/big_buck_bunny_1080p_surround.avi");

	
	time_t t = time(NULL);
	int fps = 0;
	for (int i=0; running; i++) {
		int sleep = 25;
		switch (ambiPi.mode()) {
		case AmbiPi::Off:
			sleep = 100;
			ambiPi.setColor(0,0,0);
			break;
		case AmbiPi::White:
			sleep = 100;
			ambiPi.setColor(255, 170, 40);
			break;
		case AmbiPi::Color:
			sleep = 100;
			// ambiPi.setColor(255, 170, 40);
			break;
		case AmbiPi::Rainbow:
			sleep = 25;
			ambiPi.rainbow(i);
			break;	
		case AmbiPi::TestPattern:
			sleep = 5;
			ambiPi.drawTestPattern(i, 128);
			break;
		case AmbiPi::AmbiLight:
			capture->grab();
			capture->retrieve(frame);
			// fprintf(stderr, "Frame: %dx%d\n", frame.cols, frame.rows);
			sleep = 0;
			// frame = ambiPi.createTestImage(1920,1080);
			if (!frame.empty()) {
				ambiPi.calculateAmbilightFromFrame(frame, 0.90);
			}
			break;
		default:
			break;
		}
#ifdef _DEVEL_
		ambiPi.drawGUI(frame);
#else
		drawToFrameBuffer(&fb_info, frame);

		ambiPi.render();
#endif
		usleep(1000*sleep);

		fps++;
		time_t t2 = time(NULL);
		if (t != t2) {
			fprintf(stderr, "FPS: %d\n",fps);
			t = t2;
			fps = 0;
		}
#ifdef _DEVEL_
		if  (cv::waitKey(10)=='q') {
			running = false;
		}
#endif		
	}
#else
#ifdef _DEVEL_
	cv::VideoCapture* capture = new cv::VideoCapture("/home/akw/Downloads/big_buck_bunny_1080p_surround.avi");
	//capture->set(cv::CAP_PROP_FRAME_WIDTH,  1920/2);
	//capture->set(cv::CAP_PROP_FRAME_HEIGHT, 1080/2);
#else
	cv::VideoCapture* capture = new cv::VideoCapture("/home/pi/big_buck_bunny_1080p_surround.avi");
//	cv::VideoCapture* capture = new cv::VideoCapture(0);
#endif

	cv::Mat inputFrame = ambiPi.createTestImage(1920,1080);
	// inputFrame = cv::imread("/home/pi/ambipi2.jpg", cv::IMREAD_COLOR);
	//  cv::resize(inputFrame, inputFrame, cv::Size(1920,1080), 0, 0, cv::INTER_LINEAR);
	while (running) {
		//capture->grab();
		//capture->retrieve(inputFrame);
#ifndef _DEVEL_
		usleep(416*100);
		ambiPi.calculateAmbilightFromFrame(inputFrame, 0.25);
		ambiPi.render();
#else
		//cv::resize(inputFrame, inputFrame, cv::Size(0,0), 0.5, 0.5, cv::INTER_LINEAR);
		ambiPi.guiDemo(inputFrame);
		if  (cv::waitKey(10)=='q') {
			running = false;
		}
#endif		
	}
#endif

	fprintf(stderr, "Stopping AmbiPi..\n");
	ambiPi.setColor(0,0,0);
	ambiPi.render();
	fprintf(stderr, "Stopping AmbiPi done..\n");
	return 0;
}
