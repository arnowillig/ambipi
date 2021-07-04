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

#ifdef _GUI_
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
	restServer->start(80);
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
	
	FrameBuffer fb("/dev/fb0");
	
	AmbiPi ambiPi;
	if (!ambiPi.init(1.73)) {
		return 1;
	}
	ambiPi.setFrameBuffer(&fb);

	RESTServer server(&ambiPi);
	std::thread restThread(&restServer, &server);

	cv::Mat frame;
	ambiPi.setMode(AmbiPi::AmbiLight);
	AmbiPi::Mode lastMode = AmbiPi::AmbiLight;
	
	cv::VideoCapture* capture = nullptr;
	
	time_t t = time(NULL);
	int secs = 0;
	int fps = 0;
	int sleep;
	for (int i=0; running; i++) {
		if (lastMode != ambiPi.mode()) {
			lastMode = ambiPi.mode();
			ambiPi.clearLastFrame(0,lastMode==AmbiPi::Off ? 0 : 255,0);
		}
		
		switch (ambiPi.mode()) {
		case AmbiPi::Off:
			ambiPi.setColor(0,0,0);
			//if (lastMode != AmbiPi::Off) {
			//	ambiPi.render();
			// }
			// usleep(100*1000);
			sleep = 100;
			// continue;
			break;
		case AmbiPi::White:
			sleep = 100;
			ambiPi.setColor(255, 170, 40);
			break;
		case AmbiPi::Color:
			sleep = 100;
			break;
		case AmbiPi::Rainbow:
			sleep = ambiPi.rainbow(i);
			break;
		case AmbiPi::Vegas:
			sleep = ambiPi.vegas(i);
			break;
		case AmbiPi::Knightrider:
			sleep = ambiPi.knightrider(i);
			break;
		case AmbiPi::TestPattern:
			sleep = ambiPi.drawTestPattern(i);
			break;
		case AmbiPi::AmbiLight2:
#ifdef _GUI_
			frame = fb.grabFrame(2);
#else
			frame = fb.grabFrame(12);
#endif
			ambiPi.updateCropRect(frame);
			frame = ambiPi.cropBorders(frame, false);
			sleep = 25;
			// sleep = 50;
			if (screenshot) {
				cv::Mat out;
				cv::cvtColor(frame, out, cv::COLOR_RGB2BGR);
				cv::imwrite("/home/pi/screenshot.png", out);
				screenshot = false;
			}
			if (!frame.empty()) {
				ambiPi.calculateAmbilightFromFrame(frame, true);
			}
			break;
		case AmbiPi::AmbiLight:
			if (!capture) {
				capture = new cv::VideoCapture(0);
				capture->set(cv::CAP_PROP_FRAME_WIDTH,  720);
				capture->set(cv::CAP_PROP_FRAME_HEIGHT, 480);
//				capture->set(cv::CAP_PROP_FRAME_WIDTH,  1920);
//				capture->set(cv::CAP_PROP_FRAME_HEIGHT, 1080);
				capture->set(cv::CAP_PROP_FPS, 25);
				if (!capture->isOpened()) {
					fprintf(stderr, "Could not open capture device!\n");
				}
			}
			if (!capture->grab()) {
				sleep = 250;
				fprintf(stderr, "No frame...\n");
			} else {
				capture->retrieve(frame);
				// fprintf(stderr, "Grab frame: %dx%d\n", frame.cols, frame.rows);
				if (frame.empty()) {
					delete capture;
					capture = nullptr;
					sleep = 100;
				} else {
					// cv::convert(frame, frame, cv::COLOR_BGR2RGB);
					// int interpolation = cv::INTER_LINEAR; // INTER_CUBIC
					// cv::resize(frame, frame, cv::Size(720,480), 0, 0, cv::INTER_LINEAR);
					
					ambiPi.setLastFrame(frame);
					if (ambiPi.croppingEnabled()) {
						ambiPi.updateCropRect(frame);
						ambiPi.setUpdateCropRect(false);
					}
					frame = ambiPi.cropBorders(frame, false);
					ambiPi.calculateAmbilightFromFrame(frame);
					sleep = 20;
				}
			}
			break;
		default:
			sleep = 100;
			break;
		}
#ifdef _GUI_
		ambiPi.drawGUI(frame);
#else
		// fb.drawFrame(frame);
		// drawToDispManX(frame);
//		if ((ambiPi.mode() != AmbiPi::Off)) {
			ambiPi.render();
//		}
#endif
		if (sleep>0) {
			usleep(1000*sleep);
		}

		fps++;
		time_t t2 = time(NULL);
		if (t != t2) {
			fprintf(stderr, "FPS: %d\n",fps);
			t = t2;
			fps = 0;
			if (secs++%2==0 || true) {
				ambiPi.setUpdateCropRect(true);
			}
		}
#ifdef _GUI_
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
#ifndef _GUI_
	fb.clear();
#endif
	fprintf(stderr, "Stopping AmbiPi done..\n");
	return 0;
}

