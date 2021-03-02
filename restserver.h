#ifndef RESTSERVER_H
#define RESTSERVER_H


#include <pistache/http.h>
#include <pistache/router.h>
#include <pistache/endpoint.h>

#include "ambipi.h"

using namespace std;
using namespace Pistache;

class RESTServer
{
	AmbiPi* _ambiPi;
	Rest::Router _router;
public:
	RESTServer(AmbiPi* ambiPi);
	void start(int port);

	void setAlpha(const Rest::Request &request, Http::ResponseWriter response);
	void setGamma(const Rest::Request &request, Http::ResponseWriter response);
	void setBrightness(const Rest::Request &request, Http::ResponseWriter response);
	void getBrightness(const Rest::Request &request, Http::ResponseWriter response);
	void setColor(const Rest::Request &request, Http::ResponseWriter response);
	void setMode(const Rest::Request &request, Http::ResponseWriter response);
	void getLEDs(const Rest::Request &request, Http::ResponseWriter response);
	void getScreenshot(const Rest::Request& request, Http::ResponseWriter response);
};

#endif // RESTSERVER_H
