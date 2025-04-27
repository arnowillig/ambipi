#include "restserver.h"
#include "framebuffer.h"
#include "json.hpp"
#include <algorithm>
// #include <nlohmann/json.hpp>


RESTServer::RESTServer(AmbiPi* ambiPi) : _ambiPi(ambiPi)
{
	_basePath = "/home/pi/src/ambipi/html";

	Rest::Routes::Post(_router, "/api",                     Rest::Routes::bind(&RESTServer::handlePost, this));
	
	Rest::Routes::Get(_router, "/api/display/:enabled",	Rest::Routes::bind(&RESTServer::setDisplay, this));

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


        Rest::Routes::Options(_router, "/api/*",                 Rest::Routes::bind(&RESTServer::preflight, this));
        Rest::Routes::Options(_router, "/api/*/*",                 Rest::Routes::bind(&RESTServer::preflight, this));
        Rest::Routes::Options(_router, "/api/*/*/*",                 Rest::Routes::bind(&RESTServer::preflight, this));
        Rest::Routes::Options(_router, "/api/*/*/*/*",                 Rest::Routes::bind(&RESTServer::preflight, this));
        Rest::Routes::Options(_router, "/api/*/*/*/*/*",                 Rest::Routes::bind(&RESTServer::preflight, this));
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

static void kelvinToRGB(int kelvin, int& r, int& g, int& b)
{
    double t = kelvin / 100.0;
    r = (t <= 66) ? 255 : std::clamp(int(329.698727446 * pow(t - 60, -0.1332047592)), 0, 255);
    g = (t <= 66) ? std::clamp(int(99.4708025861 * log(t) - 161.1195681661), 0, 255) : std::clamp(int(288.1221695283 * pow(t - 60, -0.0755148492)), 0, 255);
    b = (t >= 66) ? 255 : (t <= 19) ? 0 : std::clamp(int(138.5177312231 * log(t - 10) - 305.0447927307), 0, 255);
}

void RESTServer::handlePost(const Rest::Request& request, Http::ResponseWriter response)
{
	auto jsonBody = nlohmann::json::parse(request.body());
	std::string cmd = jsonBody.value("directive", "unknown");
	
//	std::string powerState = jsonBody.value("powerState", "UNKNOWN"); // Currently not used

	if (cmd == "TurnOff") {
		_ambiPi->setMode(AmbiPi::Off);
	} else if (cmd == "TurnOn") {
		_ambiPi->setMode(AmbiPi::White);
	} else if (cmd == "SetBrightness") {
		int brightness = jsonBody.value("brightness", 0);
		int bri = (brightness * 255) / 100;
		_ambiPi->setBrightness(bri);		
	} else if (cmd == "SetColor") {
		auto color = jsonBody["color_rgb"];
		int r = color[0];
		int g = color[1];
		int b = color[2];
		_ambiPi->setMode(AmbiPi::Color);
		_ambiPi->setColor(r,g,b);		
	} else if (cmd == "SetColorTemperature") {
		int colorTemp = jsonBody.value("colorTemperatureInKelvin", 2700);
		int r = 0;
		int g = 0;
		int b = 0;
		kelvinToRGB(colorTemp, r, g, b);
		_ambiPi->setMode(AmbiPi::Color);
		_ambiPi->setColor(r,g,b);
	} else {
	}
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, "");
}

void RESTServer::preflight(const Rest::Request& request, Http::ResponseWriter response)
{
        std::cout << "CORS: " << request.resource() << std::endl;
        response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
        response.headers().add<Http::Header::AccessControlAllowHeaders>("*");
        response.send(Http::Code::Ok);
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
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Not_Found);
}

void RESTServer::getScreenshot(const Rest::Request& request, Http::ResponseWriter response)
{
	(void) request;
	// cv::Mat frame = _ambiPi->frameBuffer()->grabFrame(2, true);
	cv::Mat frame = _ambiPi->lastFrame();
	frame = _ambiPi->cropBorders(frame, true);
	
	frame = _ambiPi->getDebugFrame(frame);

	std::vector<uchar> buf;
	std::vector<int> param(2);
	param[0] = cv::IMWRITE_JPEG_QUALITY;
	param[1] = 95;
	cv::imencode(".jpg", frame, buf, param);
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, std::string{buf.begin(), buf.end()}, MIME(Image, Jpeg));
	// todo ..
}

void RESTServer::getLEDs(const Rest::Request& request, Http::ResponseWriter response)
{
	(void) request;
	std::string resp = ""; // std::to_string(alpha) + "\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
}

void RESTServer::setAlpha(const Rest::Request &request, Http::ResponseWriter response)
{
	double alpha = request.param(":alpha").as<double>();
	std::string resp = std::to_string(alpha) + "\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
	_ambiPi->setAlpha(alpha);
}

void RESTServer::getAlpha(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	std::string resp = std::to_string(_ambiPi->alpha()) + "\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
}


void RESTServer::setGamma(const Rest::Request &request, Http::ResponseWriter response)
{
	double gamma = request.param(":gamma").as<double>();
	std::string resp = std::to_string(gamma) + "\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
	_ambiPi->setGamma(gamma);
}

void RESTServer::getGamma(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	std::string resp = std::to_string(_ambiPi->gamma()) + "\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
}

void RESTServer::setCropping(const Rest::Request &request, Http::ResponseWriter response)
{
	int crop = request.param(":crop").as<int>();
	std::string resp = std::to_string(crop) + "\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
	_ambiPi->setUpdateCropRect(crop ? true : false);
}

void RESTServer::getCropping(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	std::string resp = std::to_string(_ambiPi->croppingEnabled() ? 1 : 0) + "\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
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
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
	_ambiPi->setBrightness(bri);
}

void RESTServer::getBrightness(const Rest::Request& request, Http::ResponseWriter response)
{
	(void) request;
	int bri = (_ambiPi->brightness() * 100) / 255;

	std::string resp = "{ \"bri\": " + std::to_string(bri) + " }\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
}

void RESTServer::setColor(const Rest::Request& request, Http::ResponseWriter response)
{
	int r = request.param(":r").as<int>();
	int g = request.param(":g").as<int>();
	int b = request.param(":b").as<int>();
	std::string resp = std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
	_ambiPi->setMode(AmbiPi::Color);
	_ambiPi->setColor(r,g,b);
}

void RESTServer::setMode(const Rest::Request& request, Http::ResponseWriter response)
{
	std::string mode = request.param(":mode").as<std::string>();
	std::string resp = mode + "\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
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
	} else if (mode=="vegas") {
		_ambiPi->setMode(AmbiPi::Vegas);
	} else if (mode=="knightrider") {
		_ambiPi->setMode(AmbiPi::Knightrider);
	} else if (mode=="testpattern") {
		_ambiPi->setMode(AmbiPi::TestPattern);
	} else if (mode=="leftside") {
		_ambiPi->setMode(AmbiPi::LeftSide);
	} else if (mode=="rightside") {
		_ambiPi->setMode(AmbiPi::RightSide);
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
	case AmbiPi::Vegas:
		mode = "vegas"; break;
	case AmbiPi::Knightrider:
		mode = "knightrider"; break;
	case AmbiPi::TestPattern:
		mode = "testpattern"; break;
	default:
		break;
	}
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, mode + "\n");
}

void RESTServer::setDisplay(const Rest::Request &request, Http::ResponseWriter response)
{
	bool enabled = request.param(":enabled").as<bool>();
	std::string resp = enabled ? "true\n" : "false\n";
	response.send(Http::Code::Ok, resp);
	_ambiPi->setEnableDisplayVideo(enabled);
}
