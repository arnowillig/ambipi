#include "restserver.h"
#include "framebuffer.h"
#include <algorithm>



RESTServer::RESTServer(AmbiPi* ambiPi) : _ambiPi(ambiPi)
{
	Rest::Routes::Get(_router, "/api/alpha/:alpha",		Rest::Routes::bind(&RESTServer::setAlpha, this));
	Rest::Routes::Get(_router, "/api/gamma/:gamma",		Rest::Routes::bind(&RESTServer::setGamma, this));
	Rest::Routes::Get(_router, "/api/bri",			Rest::Routes::bind(&RESTServer::getBrightness, this));
	Rest::Routes::Get(_router, "/api/bri/:bri",		Rest::Routes::bind(&RESTServer::setBrightness, this));
	Rest::Routes::Get(_router, "/api/mode/:mode",		Rest::Routes::bind(&RESTServer::setMode, this));
	Rest::Routes::Get(_router, "/api/col/:r/:g/:b",		Rest::Routes::bind(&RESTServer::setColor, this));
	Rest::Routes::Get(_router, "/api/leds",			Rest::Routes::bind(&RESTServer::getLEDs, this));
	Rest::Routes::Get(_router, "/api/screenshot.jpg",	Rest::Routes::bind(&RESTServer::getScreenshot, this));
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

void RESTServer::getScreenshot(const Rest::Request& request, Http::ResponseWriter response)
{
	cv::Mat frame = _ambiPi->frameBuffer()->grabFrame(2, true);
	
	std::vector<uchar> buf;
	std::vector<int> param(2);
	param[0] = cv::IMWRITE_JPEG_QUALITY;
	param[1] = 95;
	cv::imencode(".jpg", frame, buf, param);
	response.send(Http::Code::Ok, std::string{buf.begin(), buf.end()}, MIME(Image, Jpeg));
	// todo ..
}

void RESTServer::getLEDs(const Rest::Request& request, Http::ResponseWriter response)
{
	std::string resp = ""; // std::to_string(alpha) + "\n";
	response.send(Http::Code::Ok, resp);
}

void RESTServer::setAlpha(const Rest::Request &request, Http::ResponseWriter response)
{
	double alpha = request.param(":alpha").as<double>();
	std::string resp = std::to_string(alpha) + "\n";
	response.send(Http::Code::Ok, resp);
	_ambiPi->setAlpha(alpha);
}

void RESTServer::setGamma(const Rest::Request &request, Http::ResponseWriter response)
{
	double gamma = request.param(":gamma").as<double>();
	std::string resp = std::to_string(gamma) + "\n";
	response.send(Http::Code::Ok, resp);
	_ambiPi->setGamma(gamma);
}

void RESTServer::setBrightness(const Rest::Request& request, Http::ResponseWriter response)
{
	int bri = request.param(":bri").as<int>();
	if (bri<=100) {
		bri = (bri * 255) / 100;
	}
	std::cout << "BRI:" << bri << std::endl;

	std::string resp = std::to_string(bri);
	resp.append("\n");
	response.send(Http::Code::Ok, resp);
	_ambiPi->setBrightness(bri);
}

void RESTServer::getBrightness(const Rest::Request& request, Http::ResponseWriter response)
{
	int bri = (_ambiPi->getBrightness() * 100) / 255;

	std::string resp = std::to_string(bri) + "\n";
	response.send(Http::Code::Ok, resp);
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
	} else if (mode=="ambilight") {
		_ambiPi->setMode(AmbiPi::AmbiLight);
	} else if (mode=="white") {
		_ambiPi->setMode(AmbiPi::White);
	} else if (mode=="rainbow") {
		_ambiPi->setMode(AmbiPi::Rainbow);
	} else if (mode=="testpattern") {
		_ambiPi->setMode(AmbiPi::TestPattern);
	}
}
