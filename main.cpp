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

static void restServer(RESTServer* restServer)
{
	restServer->start(9080);
}


int main(int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	signal(SIGINT,  signalHandler);
	signal(SIGTERM, signalHandler);
	signal(SIGUSR1, signalHandler);
	
	fprintf(stderr, "AmbiPi\n");
	
	AmbiPi ambiPi;
	if (!ambiPi.init(0)) {
		return 1;
	}

	RESTServer server(&ambiPi);
	std::thread restThread(&restServer, &server);

#if 1
	cv::Mat frame;
	fprintf(stderr, "Setting color..\n");
	ambiPi.setMode(AmbiPi::White);
	ambiPi.setMode(AmbiPi::AmbiLight);
	// ambiPi.setColor      (255, 170, 40);
	// ambiPi.render();
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
			sleep  = 25;
			ambiPi.rainbow(i);
			break;	
		case AmbiPi::TestPattern:
			sleep  = 5;
			ambiPi.drawTestPattern(i, 128);
			break;
		case AmbiPi::AmbiLight:
			sleep  = 10;
			frame = ambiPi.createTestImage(960,540);
			ambiPi.calculateAmbilightFromFrame(frame, 0.95);
			break;
		default:
			break;
		}
#ifdef _DEVEL_
		ambiPi.drawGUI(frame);
#else
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
		if  (cv::waitKey(10)=='q') {
			running = false;
		}
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

	cv::Mat inputFrame = ambiPi.createTestImage(640,480);
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
