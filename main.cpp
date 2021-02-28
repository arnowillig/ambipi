#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include <pistache/endpoint.h>

#include "ambipi.h"

using namespace Pistache;

class HelloHandler : public Http::Handler {
public:

    HTTP_PROTOTYPE(HelloHandler)

    void onRequest(const Http::Request& request, Http::ResponseWriter response) {
        response.send(Http::Code::Ok, "Hello, World\n");
    }
};


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

int main(int argc, char *argv[])
{
	(void) argc;
	(void) argv;

//	signal(SIGINT,  signalHandler);
	signal(SIGTERM, signalHandler);
	signal(SIGUSR1, signalHandler);
	
	fprintf(stderr, "AmbiPi\n");
	
    Address addr(Ipv4::any(), Port(9080));

    auto opts = Http::Endpoint::options().threads(1);
    Http::Endpoint server(addr);
    server.init(opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.serve();	
    return 0;

	
	AmbiPi ambiPi;
	if (!ambiPi.init(0)) {
#ifndef _DEVEL_
		return 1;
#endif
	}


#if 1
	fprintf(stderr, "Setting color..\n");
	int bri = 255;
	ambiPi.setColor      (255, 170, 40);
	ambiPi.render();
	time_t t = time(NULL);
	int fps = 0;
	for (int i=0; running; i++) {
		// 
		//ambiPi.drawTestPattern(i, bri);
		// 
		//
	// 
		ambiPi.rainbow(i);
		usleep(25);
		// 
		//usleep(1000*1000);
		fps++;
		time_t t2 = time(NULL);
		if (t != t2) {
			fprintf(stderr, "FPS: %d\n",fps);
			t = t2;
			fps = 0;
		}
		
	}
#else
#ifndef _DEVEL_
	cv::VideoCapture* capture = new cv::VideoCapture("/home/pi/big_buck_bunny_1080p_surround.avi");
#else
//	cv::VideoCapture* capture = new cv::VideoCapture(0);
	cv::VideoCapture* capture = new cv::VideoCapture("/home/akw/Downloads/big_buck_bunny_1080p_surround.avi");
#endif

	cv::Mat inputFrame;
	inputFrame = cv::imread("/home/pi/ambipi2.jpg", cv::IMREAD_COLOR);
	cv::resize(inputFrame, inputFrame, cv::Size(1920,1080), 0, 0, cv::INTER_LINEAR);
	while (running) {
		capture->grab();
		capture->retrieve(inputFrame);
		usleep(416*100);
#ifndef _DEVEL_
		ambiPi.calculateAmbilightFromFrame(inputFrame, 0.25);
		ambiPi.render();
#else
//		cv::resize(inputFrame, inputFrame, cv::Size(0,0), 0.5, 0.5, cv::INTER_LINEAR);
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
