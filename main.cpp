#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include <algorithm>

#include <pistache/http.h>
#include <pistache/router.h>
#include <pistache/endpoint.h>

#include <thread>

#include "ambipi.h"

using namespace std;
using namespace Pistache;

AmbiPi ambiPi;

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

void setBrightness(const Rest::Request& request, Http::ResponseWriter response)
{
	int bri = request.param(":bri").as<int>();
	std::cout << "BRI:" << bri << std::endl;
	
	char buf[16];
	sprintf(buf,"%d",bri);
	std::string resp = std::to_string(bri);
	resp.append("\n");
	response.send(Http::Code::Ok, resp);
	ambiPi.setBrightness(bri);
}

void setColor(const Rest::Request& request, Http::ResponseWriter response)
{
	int r = request.param(":r").as<int>();
	int g = request.param(":g").as<int>();
	int b = request.param(":b").as<int>();
	std::string resp = std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "\n";
	response.send(Http::Code::Ok, resp);
	ambiPi.setMode(AmbiPi::Color);
	ambiPi.setColor(r,g,b);
}

void setMode(const Rest::Request& request, Http::ResponseWriter response)
{
	std::string mode = request.param(":mode").as<std::string>();
	std::string resp = mode + "\n";
	response.send(Http::Code::Ok, resp);
	if (mode=="off") {
		ambiPi.setMode(AmbiPi::Off);
	} else if (mode=="white") {
		ambiPi.setMode(AmbiPi::White);
	} else if (mode=="rainbow") {
		ambiPi.setMode(AmbiPi::Rainbow);
	}
	if (mode=="testpattern") {
		ambiPi.setMode(AmbiPi::TestPattern);
	}
}

void restServer()
{
	using namespace Rest;

	Address addr(Ipv4::any(), Port(9080));

	Rest::Router router;
	
	Routes::Get(router, "/api/bri/:bri", Routes::bind(&setBrightness));
	Routes::Get(router, "/api/mode/:mode", Routes::bind(&setMode));
	Routes::Get(router, "/api/col/:r/:g/:b", Routes::bind(&setColor));
	

	auto opts = Http::Endpoint::options().threads(1).flags(Tcp::Options::ReuseAddr);
	
	Http::Endpoint server(addr);
	server.init(opts);
	server.setHandler(router.handler());
	server.serve();	
}


int main(int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	signal(SIGINT,  signalHandler);
	signal(SIGTERM, signalHandler);
	signal(SIGUSR1, signalHandler);
	
	fprintf(stderr, "AmbiPi\n");
	
	std::thread restThread(&restServer); 
	
	
	if (!ambiPi.init(0)) {
		return 1;
	}
#if 1
	fprintf(stderr, "Setting color..\n");
	ambiPi.setMode(AmbiPi::White);
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
		default:
			break;
		}
		ambiPi.render();
		usleep(1000*sleep);

		fps++;
		time_t t2 = time(NULL);
		if (t != t2) {
			fprintf(stderr, "FPS: %d\n",fps);
			t = t2;
			fps = 0;
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

	cv::Mat inputFrame;
	// inputFrame = cv::imread("/home/pi/ambipi2.jpg", cv::IMREAD_COLOR);
	//  cv::resize(inputFrame, inputFrame, cv::Size(1920,1080), 0, 0, cv::INTER_LINEAR);
	while (running) {
		capture->grab();
		capture->retrieve(inputFrame);
#ifndef _DEVEL_
		usleep(416*100);
		ambiPi.calculateAmbilightFromFrame(inputFrame, 0.25);
		ambiPi.render();
#else
		cv::resize(inputFrame, inputFrame, cv::Size(0,0), 0.5, 0.5, cv::INTER_LINEAR);
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
