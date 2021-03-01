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
#define TEST_VIDEO "/home/pi/Videos/big_buck_bunny_1080p_surround.avi"
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
	const char* testVideo;
	
	if (argc>1) {
		testVideo = argv[1];
	} else {
		testVideo = TEST_VIDEO;
	}

	signal(SIGINT,  signalHandler);
	signal(SIGTERM, signalHandler);
	signal(SIGUSR1, signalHandler);
	
	fprintf(stderr, "AmbiPi\n");
	
#ifndef _DEVEL_
	FrameBuffer fb("/dev/fb0");
#endif
	
	AmbiPi ambiPi;
	if (!ambiPi.init(1.0)) {
		return 1;
	}

	RESTServer server(&ambiPi);
	std::thread restThread(&restServer, &server);

	cv::Mat frame;
	fprintf(stderr, "Setting color..\n");
	ambiPi.setMode(AmbiPi::AmbiLight);
	
	cv::VideoCapture* capture = nullptr;
	
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
			break;
		case AmbiPi::Rainbow:
			sleep = 5;
			ambiPi.rainbow(i);
			break;
		case AmbiPi::TestPattern:
			sleep = 5;
			ambiPi.drawTestPattern(i, 128);
			break;
		case AmbiPi::AmbiLight:
			if (!capture) {
				capture = new cv::VideoCapture(testVideo);
			}
			if (!pauseVideo) {
				// frame = ambiPi.createTestImage(1920,1080);

				capture->grab();
				capture->retrieve(frame);
				if (frame.empty()) {
					delete capture;
					capture = nullptr;
				} else if (frame.cols != 1920) {
					cv::resize(frame, frame, cv::Size(1920,1080), 0, 0, cv::INTER_LINEAR);
				}
				sleep = 0;
			} else {
				sleep = 100;
			}
			if (!frame.empty()) {
				ambiPi.calculateAmbilightFromFrame(frame);
			}
			break;
		default:
			break;
		}
#ifdef _DEVEL_
		ambiPi.drawGUI(frame);
#else
		fb.drawFrame(frame);
		// drawToDispManX(frame);
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

