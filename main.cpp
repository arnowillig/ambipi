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
#include "atvremote.h"
#include <cstring>

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
	// One-time Android TV Remote pairing for the JMGO beamer:
	//   sudo ambipi --pair-beamer   (enter the code shown on the projector)
	if (argc > 1 && strcmp(argv[1], "--pair-beamer") == 0) {
		AtvRemote atv;
		return atv.pair() ? 0 : 1;
	}

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
	ambiPi.setMode(AmbiPi::White);
	AmbiPi::Mode lastMode = ambiPi.mode();

	cv::VideoCapture* capture = nullptr;
	time_t lastGoodFrame = 0;   // last time a frame was successfully grabbed
	time_t lastRecovery  = 0;   // last time we attempted USB/capture recovery
	time_t lastCrop      = 0;   // last time the letterbox crop rect was recomputed

	time_t t = time(NULL);
	int fps = 0;
	int sleep;
	int cnt = 0;
	for (int i=0; running; i++) {
		if (lastMode != ambiPi.mode()) {
			lastMode = ambiPi.mode();
			ambiPi.clearLastFrame(0, lastMode==AmbiPi::Off ? 0 : 255, 0);
			cnt = 0;
		}
		
		switch (ambiPi.mode()) {
		case AmbiPi::Off:
			ambiPi.setColor(0,0,0);
			sleep = 100;
			break;
		case AmbiPi::White:
			sleep = 100;
			ambiPi.setColor(255, 170, 40);
			break;
		case AmbiPi::Color:
			sleep = 100;
			break;
		case AmbiPi::LeftSide:
			sleep = ambiPi.goal(cnt, true);
			break;
		case AmbiPi::RightSide:
			sleep = ambiPi.goal(cnt, false);
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
				cv::imwrite("/var/lib/ambipi/screenshot.png", out);
				screenshot = false;
			}
			if (!frame.empty()) {
				ambiPi.calculateAmbilightFromFrame(frame, true);
			}
			break;
		case AmbiPi::AmbiLight:
			// Capture resolution changed via the web UI -> reopen with the new mode.
			if (capture && ambiPi.takeCaptureResDirty()) {
				delete capture;
				capture = nullptr;
			}
			if (!capture) {
				// Force the V4L2 backend. OpenCV otherwise auto-selects GStreamer,
				// which mis-decodes this "AV TO USB2.0" (EasyCap) YUYV stream and
				// corrupts the colors (green cast / R-B mix). V4L2 decodes correctly.
				capture = new cv::VideoCapture(0, cv::CAP_V4L2);
#if 1
				capture->set(cv::CAP_PROP_FRAME_WIDTH,  ambiPi.getCaptureWidth());
				capture->set(cv::CAP_PROP_FRAME_HEIGHT, ambiPi.getCaptureHeight());
#else
				capture->set(cv::CAP_PROP_FRAME_WIDTH,  720);
				capture->set(cv::CAP_PROP_FRAME_HEIGHT, 480);
#endif				
//				capture->set(cv::CAP_PROP_FRAME_WIDTH,  1920);
//				capture->set(cv::CAP_PROP_FRAME_HEIGHT, 1080);
				capture->set(cv::CAP_PROP_FPS, 30);
//				capture->open(0, cv::CAP_V4L2);
				
				if (!capture->isOpened()) {
					fprintf(stderr, "Could not open capture device!\n");
				}
			}
			if (!capture->grab()) {
				sleep = 250;
				// Rate-limit this: when the source (AppleTV) is off there is no
				// signal and grab() fails ~4x/s, which otherwise floods the
				// journal (it once grew to 1.7 GB). Log at most once / 10 s.
				static time_t lastNoFrameLog = 0;
				static int noFrameCount = 0;
				noFrameCount++;
				time_t nowt = time(NULL);
				if (nowt - lastNoFrameLog >= 10) {
					fprintf(stderr, "No frame (x%d in last %lds)...\n",
						noFrameCount, lastNoFrameLog ? (long)(nowt - lastNoFrameLog) : 0L);
					lastNoFrameLog = nowt;
					noFrameCount = 0;
				}
				// Auto-recovery: the cheap EasyCap grabber drops off USB and won't
				// re-enumerate ("Cannot enable"). If no frame has arrived for a while,
				// power-cycle its USB port (uhubctl, via config) and reopen the device.
				if (lastGoodFrame == 0) lastGoodFrame = nowt;
				if (nowt - lastGoodFrame >= 15 && nowt - lastRecovery >= 45) {
					fprintf(stderr, "Capture dead for %lds — recovering (USB port-cycle + reopen)\n",
						(long)(nowt - lastGoodFrame));
					ambiPi.cycleCaptureUsbPort();   // no-op if disabled in config
					delete capture;
					capture = nullptr;              // forces reopen next iteration
					lastRecovery  = nowt;
					lastGoodFrame = nowt;           // grace period after recovery
					sleep = 1000;                   // let USB re-enumerate before reopen
				}
			} else {
				capture->retrieve(frame);
				// fprintf(stderr, "Grab frame: %dx%d\n", frame.cols, frame.rows);
				if (frame.empty()) {
					delete capture;
					capture = nullptr;
					sleep = 100;
				} else {
					// Letterbox/black-bar crop (toggle /api/crop, default off): recompute
					// the content rect ~1x/s (Canny is costly), then stretch content to fill
					// every frame. Done before setLastFrame so the preview mirrors the LEDs.
					if (ambiPi.croppingEnabled()) {
						time_t nowc = time(NULL);
						if (nowc != lastCrop) { ambiPi.updateCropRect(frame); lastCrop = nowc; }
						frame = ambiPi.cropBorders(frame, false);
					}
					// HDR->SDR compensation now happens inside calculateAmbilightFromFrame,
					// on the small downsampled edge strips (cheap) — not on the full frame.
					lastGoodFrame = time(NULL);
					ambiPi.setLastFrame(frame);
					ambiPi.calculateAmbilightFromFrame(frame);
					if (ambiPi.getEnableDisplayVideo()) {
						ambiPi.calculateDisplayFrameFromFrame(frame);
					}
					if (ambiPi.getEnableGameWallAmbilight()) {
						ambiPi.calculateGameWallFrameFromFrame(frame);
						//ambiPi.calculateKickerLightsFromFrame(frame);
					}
					sleep = 40;
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
		ambiPi.render();
#endif
		if (sleep>0) {
			usleep(1000*sleep);
		}

		fps++;
		time_t t2 = time(NULL);
		if (t != t2) {
			t = t2;
			fps = 0;
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
		cnt++;
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

