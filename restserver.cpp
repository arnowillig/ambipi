#include "restserver.h"
#include "framebuffer.h"
#include <algorithm>



RESTServer::RESTServer(AmbiPi* ambiPi) : _ambiPi(ambiPi)
{
	_basePath = "/home/pi/src/ambipi/html";

	Rest::Routes::Get(_router, "/api/alpha/:alpha",		Rest::Routes::bind(&RESTServer::setAlpha, this));
	Rest::Routes::Get(_router, "/api/alpha",		Rest::Routes::bind(&RESTServer::getAlpha, this));

	Rest::Routes::Get(_router, "/api/gamma/:gamma",		Rest::Routes::bind(&RESTServer::setGamma, this));
	Rest::Routes::Get(_router, "/api/gamma",		Rest::Routes::bind(&RESTServer::getGamma, this));

	Rest::Routes::Get(_router, "/api/bri/:bri",		Rest::Routes::bind(&RESTServer::setBrightness, this));
	Rest::Routes::Get(_router, "/api/bri",			Rest::Routes::bind(&RESTServer::getBrightness, this));

	Rest::Routes::Get(_router, "/api/mode/:mode",		Rest::Routes::bind(&RESTServer::setMode, this));
	Rest::Routes::Get(_router, "/api/mode",			Rest::Routes::bind(&RESTServer::getMode, this));

	Rest::Routes::Get(_router, "/api/crop/:crop",		Rest::Routes::bind(&RESTServer::setCropping, this));
	Rest::Routes::Get(_router, "/api/crop",			Rest::Routes::bind(&RESTServer::getCropping, this));

	Rest::Routes::Get(_router, "/api/col/:r/:g/:b",		Rest::Routes::bind(&RESTServer::setColor, this));
	Rest::Routes::Get(_router, "/api/leds",			Rest::Routes::bind(&RESTServer::getLEDs, this));
	Rest::Routes::Get(_router, "/api/screenshot.jpg",	Rest::Routes::bind(&RESTServer::getScreenshot, this));

	Rest::Routes::Get(_router, "/index.html",		Rest::Routes::bind(&RESTServer::getStaticHTML, this));
	Rest::Routes::Get(_router, "/",				Rest::Routes::bind(&RESTServer::getStaticHTML, this));
	Rest::Routes::Get(_router, "/static/*",                 Rest::Routes::bind(&RESTServer::getStaticHTML, this));
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

void RESTServer::getStaticHTML(const Rest::Request& request, Http::ResponseWriter response)
{
	std::cout << "REST: " << request.resource() << std::endl;
	if (request.resource()=="/" || request.resource()=="/index.html") {
		Http::serveFile(response, _basePath + "/index.html");
		return;
	}
	if (request.resource().find("/static/", 0) == 0) {
		Http::serveFile(response, _basePath + request.resource().substr(7));
		return;
	}
	response.send(Http::Code::Not_Found);
}

void RESTServer::getScreenshot(const Rest::Request& request, Http::ResponseWriter response)
{
	(void) request;
	// cv::Mat frame = _ambiPi->frameBuffer()->grabFrame(2, true);
	cv::Mat frame = _ambiPi->lastFrame();
	frame = _ambiPi->cropBorders(frame, true);

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
	(void) request;
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

void RESTServer::getAlpha(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	std::string resp = std::to_string(_ambiPi->alpha()) + "\n";
	response.send(Http::Code::Ok, resp);
}


void RESTServer::setGamma(const Rest::Request &request, Http::ResponseWriter response)
{
	double gamma = request.param(":gamma").as<double>();
	std::string resp = std::to_string(gamma) + "\n";
	response.send(Http::Code::Ok, resp);
	_ambiPi->setGamma(gamma);
}

void RESTServer::getGamma(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	std::string resp = std::to_string(_ambiPi->gamma()) + "\n";
	response.send(Http::Code::Ok, resp);
}

void RESTServer::setCropping(const Rest::Request &request, Http::ResponseWriter response)
{
	int crop = request.param(":crop").as<int>();
	std::string resp = std::to_string(crop) + "\n";
	response.send(Http::Code::Ok, resp);
	_ambiPi->setEnableCropping(crop ? true : false);
}

void RESTServer::getCropping(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	std::string resp = std::to_string(_ambiPi->croppingEnabled() ? 1 : 0) + "\n";
	response.send(Http::Code::Ok, resp);
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
	(void) request;
	int bri = (_ambiPi->brightness() * 100) / 255;

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
	} else if (mode=="color") {
		_ambiPi->setMode(AmbiPi::Color);
	} else if (mode=="rainbow") {
		_ambiPi->setMode(AmbiPi::Rainbow);
	} else if (mode=="testpattern") {
		_ambiPi->setMode(AmbiPi::TestPattern);
	}
}

void RESTServer::getMode(const Rest::Request& request, Http::ResponseWriter response)
{
	(void) request;
	std::string mode;
	switch (_ambiPi->mode()) {
	case AmbiPi::Off:
		mode = "off"; break;
	case AmbiPi::AmbiLight:
		mode = "ambilight"; break;
	case AmbiPi::AmbiLight2:
		mode = "ambilight2"; break;
	case AmbiPi::White:
		mode = "white"; break;
	case AmbiPi::Color:
		mode = "color"; break;
	case AmbiPi::Rainbow:
		mode = "rainbow"; break;
	case AmbiPi::TestPattern:
		mode = "testpattern"; break;
	default:
		break;
	}
	response.send(Http::Code::Ok, mode + "\n");
}
