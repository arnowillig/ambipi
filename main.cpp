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
#include "framebuffer.h"

#ifdef _DEVEL_
#define TEST_VIDEO "/home/akw/Downloads/big_buck_bunny_1080p_surround.avi"
#else
// #define TEST_VIDEO "/home/pi/Videos/big_buck_bunny_1080p_surround.avi"
// #define TEST_VIDEO "/home/pi/Videos/Ambilight-Color-Wheel-Test-2.mp4"
#define TEST_VIDEO "/home/pi/Videos/Avatar.Ambitest.mkv"

#if HAVE_DISPMANX
#include <bcm_host.h>
#endif

#endif

using namespace std;
using namespace Pistache;

static bool running = true;
static bool pauseVideo = false;
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

static void restServer(RESTServer* restServer)
{
	restServer->start(9080);
}

int main(int argc, char *argv[])
{
	(void) argc;
	(void) argv;
	
	const char* testVideo = TEST_VIDEO;
	
	if (argc>1) {
		testVideo = argv[1];
	}

	signal(SIGINT,  signalHandler);
	signal(SIGTERM, signalHandler);
	signal(SIGUSR1, signalHandler);
	
	fprintf(stderr, "AmbiPi\n");
	
#ifndef _DEVEL_
	FrameBuffer fb("/dev/fb0");
#endif
	
	AmbiPi ambiPi;
	if (!ambiPi.init(0)) {
		return 1;
	}

	RESTServer server(&ambiPi);
	std::thread restThread(&restServer, &server);

	cv::Mat frame;
	fprintf(stderr, "Setting color..\n");
	// ambiPi.setMode(AmbiPi::White);
	ambiPi.setMode(AmbiPi::AmbiLight);
	
	cv::VideoCapture* capture = new cv::VideoCapture(testVideo);
	
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
			if (!pauseVideo) {
				capture->grab();
				capture->retrieve(frame);
				if (!frame.empty() && frame.cols != 1920) {
					cv::resize(frame, frame, cv::Size(1920,1080), 0, 0, cv::INTER_LINEAR);
				}
			}
			sleep = 0;
			// frame = ambiPi.createTestImage(1920,1080);
			if (!frame.empty()) {
				ambiPi.calculateAmbilightFromFrame(frame, 0.80);
			}
			break;
		default:
			break;
		}
#ifdef _DEVEL_
		ambiPi.drawGUI(frame);
#else
		fb.drawFrame(frame);
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
		int key = cv::waitKey(10);
		if  (key == 'q') {
			running = false;
		}
		if  (key == ' ') {
			pauseVideo = !pauseVideo;
		}
#endif
	}

	fprintf(stderr, "Stopping AmbiPi..\n");
	ambiPi.setColor(0,0,0);
	ambiPi.render();
#ifndef _DEVEL_
	fb.clear();
#endif
	fprintf(stderr, "Stopping AmbiPi done..\n");
	return 0;
}

#if HAVE_DISPMANX
int dispmanx(void)
{
	bcm_host_init();

	DISPMANX_DISPLAY_HANDLE_T display = vc_dispmanx_display_open(0);

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
	ret = vc_dispmanx_display_close(display);
	assert( ret == 0 );

	return 0;
}
#endif
