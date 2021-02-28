#include "restserver.h"
#include <algorithm>



RESTServer::RESTServer(AmbiPi* ambiPi) : _ambiPi(ambiPi)
{
	Rest::Routes::Get(_router, "/api/bri/:bri",	Rest::Routes::bind(&RESTServer::setBrightness, this));
	Rest::Routes::Get(_router, "/api/mode/:mode",	Rest::Routes::bind(&RESTServer::setMode, this));
	Rest::Routes::Get(_router, "/api/col/:r/:g/:b",	Rest::Routes::bind(&RESTServer::setColor, this));
}

void RESTServer::start(int port)
{
	Address addr(Ipv4::any(), Port(port));
	auto opts = Http::Endpoint::options().threads(1).flags(Tcp::Options::ReuseAddr);
	Http::Endpoint server(addr);
	server.init(opts);
	server.setHandler(_router.handler());
	server.serve();
}

void RESTServer::setBrightness(const Rest::Request& request, Http::ResponseWriter response)
{
	int bri = request.param(":bri").as<int>();
	std::cout << "BRI:" << bri << std::endl;

	char buf[16];
	sprintf(buf,"%d",bri);
	std::string resp = std::to_string(bri);
	resp.append("\n");
	response.send(Http::Code::Ok, resp);
	_ambiPi->setBrightness(bri);
}

void RESTServer::setColor(const Rest::Request& request, Http::ResponseWriter response)
{
	int r = request.param(":r").as<int>();
	int g = request.param(":g").as<int>();
	int b = request.param(":b").as<int>();
	std::string resp = std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "\n";
	response.send(Http::Code::Ok, resp);
	_ambiPi->setMode(AmbiPi::Color);
	_ambiPi->setColor(r,g,b);
}

void RESTServer::setMode(const Rest::Request& request, Http::ResponseWriter response)
{
	std::string mode = request.param(":mode").as<std::string>();
	std::string resp = mode + "\n";
	response.send(Http::Code::Ok, resp);
	if (mode=="off") {
		_ambiPi->setMode(AmbiPi::Off);
	} else if (mode=="white") {
		_ambiPi->setMode(AmbiPi::White);
	} else if (mode=="rainbow") {
		_ambiPi->setMode(AmbiPi::Rainbow);
	} else if (mode=="testpattern") {
		_ambiPi->setMode(AmbiPi::TestPattern);
	}
}
