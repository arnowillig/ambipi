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
	std::string _basePath;
public:
	RESTServer(AmbiPi* ambiPi);
	void start(int port);

	void preflight(const Rest::Request &request, Http::ResponseWriter response);
	void setAlpha(const Rest::Request &request, Http::ResponseWriter response);
	void getAlpha(const Rest::Request &request, Http::ResponseWriter response);
	void setGamma(const Rest::Request &request, Http::ResponseWriter response);
	void getGamma(const Rest::Request &request, Http::ResponseWriter response);
	void setBrightness(const Rest::Request &request, Http::ResponseWriter response);
	void getBrightness(const Rest::Request &request, Http::ResponseWriter response);
	void setCropping(const Rest::Request &request, Http::ResponseWriter response);
	void getCropping(const Rest::Request &request, Http::ResponseWriter response);
	void setColor(const Rest::Request &request, Http::ResponseWriter response);
	void setMode(const Rest::Request &request, Http::ResponseWriter response);
	void getMode(const Rest::Request &request, Http::ResponseWriter response);
	void getLEDs(const Rest::Request &request, Http::ResponseWriter response);
	void getScreenshot(const Rest::Request& request, Http::ResponseWriter response);
	void getStaticHTML(const Rest::Request& request, Http::ResponseWriter response);
	void setDisplay(const Rest::Request &request, Http::ResponseWriter response);

};

#endif // RESTSERVER_H

