#include "restserver.h"
#include "framebuffer.h"
#include "json.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
// #include <nlohmann/json.hpp>


RESTServer::RESTServer(AmbiPi* ambiPi) : _ambiPi(ambiPi)
{
	_basePath = "/usr/share/ambipi/html";

	Rest::Routes::Post(_router, "/api",                     Rest::Routes::bind(&RESTServer::handlePost, this));
	
	Rest::Routes::Get(_router, "/api/display/:enabled",	Rest::Routes::bind(&RESTServer::setDisplay, this));
	Rest::Routes::Get(_router, "/api/table/:enabled",	Rest::Routes::bind(&RESTServer::setGamingTable, this));
	Rest::Routes::Get(_router, "/api/gamewall/:enabled", Rest::Routes::bind(&RESTServer::setGameWallAmbilight, this));
	Rest::Routes::Get(_router, "/api/gamewall",        Rest::Routes::bind(&RESTServer::getGameWallAmbilight, this));

	Rest::Routes::Get(_router, "/api/hdr/:enabled", Rest::Routes::bind(&RESTServer::setHdrComp, this));
	Rest::Routes::Get(_router, "/api/hdr",          Rest::Routes::bind(&RESTServer::getHdrComp, this));
	Rest::Routes::Get(_router, "/api/hdrsat/:v",  Rest::Routes::bind(&RESTServer::setHdrSat, this));
	Rest::Routes::Get(_router, "/api/hdrsat",     Rest::Routes::bind(&RESTServer::getHdrSat, this));
	Rest::Routes::Get(_router, "/api/hdrtint/:v", Rest::Routes::bind(&RESTServer::setHdrTint, this));
	Rest::Routes::Get(_router, "/api/hdrtint",    Rest::Routes::bind(&RESTServer::getHdrTint, this));
	Rest::Routes::Get(_router, "/api/hdrtemp/:v", Rest::Routes::bind(&RESTServer::setHdrTemp, this));
	Rest::Routes::Get(_router, "/api/hdrtemp",    Rest::Routes::bind(&RESTServer::getHdrTemp, this));

	Rest::Routes::Get(_router, "/api/vertex/info",            Rest::Routes::bind(&RESTServer::getVertexInfo, this));
	Rest::Routes::Get(_router, "/api/vertex/get/:key",        Rest::Routes::bind(&RESTServer::getVertex, this));
	Rest::Routes::Get(_router, "/api/vertex/set/:key/:value", Rest::Routes::bind(&RESTServer::setVertex, this));
	Rest::Routes::Get(_router, "/api/vertex/hotplug",         Rest::Routes::bind(&RESTServer::hotplugVertex, this));

	Rest::Routes::Get(_router, "/api/capres",       Rest::Routes::bind(&RESTServer::getCaptureRes, this));
	Rest::Routes::Get(_router, "/api/capres/:w/:h", Rest::Routes::bind(&RESTServer::setCaptureRes, this));

	Rest::Routes::Get(_router, "/api/beamer/off", Rest::Routes::bind(&RESTServer::beamerOff, this));
	Rest::Routes::Get(_router, "/api/beamer/on",  Rest::Routes::bind(&RESTServer::beamerOn, this));
	Rest::Routes::Get(_router, "/api/beamer/volup",     Rest::Routes::bind(&RESTServer::beamerVolUp, this));
	Rest::Routes::Get(_router, "/api/beamer/voldown",   Rest::Routes::bind(&RESTServer::beamerVolDown, this));
	Rest::Routes::Get(_router, "/api/beamer/mute",      Rest::Routes::bind(&RESTServer::beamerMute, this));
	Rest::Routes::Get(_router, "/api/beamer/playpause", Rest::Routes::bind(&RESTServer::beamerPlayPause, this));

	Rest::Routes::Get(_router, "/api/appletv/on",  Rest::Routes::bind(&RESTServer::appleTvOn, this));
	Rest::Routes::Get(_router, "/api/appletv/off", Rest::Routes::bind(&RESTServer::appleTvOff, this));

	Rest::Routes::Get(_router, "/api/shutter",                Rest::Routes::bind(&RESTServer::getShutterStatus, this));
	Rest::Routes::Get(_router, "/api/shutter/open",           Rest::Routes::bind(&RESTServer::shutterOpen,  this));
	Rest::Routes::Get(_router, "/api/shutter/open/:channel",  Rest::Routes::bind(&RESTServer::shutterOpen,  this));
	Rest::Routes::Get(_router, "/api/shutter/close",          Rest::Routes::bind(&RESTServer::shutterClose, this));
	Rest::Routes::Get(_router, "/api/shutter/close/:channel", Rest::Routes::bind(&RESTServer::shutterClose, this));
	Rest::Routes::Get(_router, "/api/shutter/halt",           Rest::Routes::bind(&RESTServer::shutterHalt,  this));
	Rest::Routes::Get(_router, "/api/shutter/halt/:channel",  Rest::Routes::bind(&RESTServer::shutterHalt,  this));

	Rest::Routes::Get(_router, "/api/display",		Rest::Routes::bind(&RESTServer::getDisplay, this));
	Rest::Routes::Get(_router, "/api/table",		Rest::Routes::bind(&RESTServer::getGamingTable, this));

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
		if (r==0 && g==0 && b==255) { //
			_ambiPi->setMode(AmbiPi::AmbiLight);
		}
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
		const std::string rel = request.resource().substr(7);
		// Reject path traversal: never let a request escape _basePath.
		if (rel.find("..") != std::string::npos) {
			response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
			response.send(Http::Code::Forbidden);
			return;
		}
		Http::serveFile(response, _basePath + rel);
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
	// getDebugFrame normalizes to 960x540 itself — no pre-resize needed (and it
	// keeps HDR comp on the smaller image).
	frame = _ambiPi->getDebugFrame(frame);

	std::vector<uchar> buf;
	std::vector<int> param(2);
	param[0] = cv::IMWRITE_JPEG_QUALITY;
	param[1] = 75;
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

void RESTServer::setGameWallAmbilight(const Rest::Request &request, Http::ResponseWriter response)
{
	bool enabled = request.param(":enabled").as<bool>();
	std::string resp = enabled ? "true\n" : "false\n";
	response.send(Http::Code::Ok, resp);
	_ambiPi->setEnableGameWallAmbilight(enabled);
}

void RESTServer::getGameWallAmbilight(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	std::string resp = _ambiPi->getEnableGameWallAmbilight() ? "true\n" : "false\n";
	response.send(Http::Code::Ok, resp);
}

void RESTServer::setHdrComp(const Rest::Request &request, Http::ResponseWriter response)
{
	bool enabled = request.param(":enabled").as<bool>();
	std::string resp = enabled ? "true\n" : "false\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
	_ambiPi->setHdrComp(enabled);
}

void RESTServer::getHdrComp(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	std::string resp = _ambiPi->getHdrComp() ? "true\n" : "false\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
}

void RESTServer::setHdrSat(const Rest::Request &request, Http::ResponseWriter response)
{
	float v = request.param(":v").as<double>();
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, std::to_string(v) + "\n");
	_ambiPi->setHdrSat(v);
}
void RESTServer::getHdrSat(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, std::to_string(_ambiPi->getHdrSat()) + "\n");
}

void RESTServer::setHdrTint(const Rest::Request &request, Http::ResponseWriter response)
{
	float v = request.param(":v").as<double>();
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, std::to_string(v) + "\n");
	_ambiPi->setHdrTint(v);
}
void RESTServer::getHdrTint(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, std::to_string(_ambiPi->getHdrTint()) + "\n");
}

void RESTServer::setHdrTemp(const Rest::Request &request, Http::ResponseWriter response)
{
	float v = request.param(":v").as<double>();
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, std::to_string(v) + "\n");
	_ambiPi->setHdrTemp(v);
}
void RESTServer::getHdrTemp(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, std::to_string(_ambiPi->getHdrTemp()) + "\n");
}

// --- HDFury Vertex serial control (FTDI on 3.5mm RS232 jack) ---------------

void RESTServer::getVertexInfo(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	std::string resp = _vertex.infoJson();
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
}

void RESTServer::getVertex(const Rest::Request &request, Http::ResponseWriter response)
{
	std::string key = request.param(":key").as<std::string>();
	std::string resp = _vertex.get(key) + "\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
}

void RESTServer::setVertex(const Rest::Request &request, Http::ResponseWriter response)
{
	std::string key   = request.param(":key").as<std::string>();
	std::string value = request.param(":value").as<std::string>();
	bool ok = _vertex.set(key, value);
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, ok ? "ok\n" : "error\n");
}

void RESTServer::hotplugVertex(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	std::string resp = _vertex.command("hotplug") + "\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
}

// --- Becker Centronic roller-shutter / projection-screen control -----------
// Native port of /usr/local/bin/{shutter_open,shutter_close,shutter_halt}.sh.
// open == DOWN (screen rolls down), close == UP, halt == stop. Optional
// :channel ("[unit:]channel") defaults to "1" (unit 1737b, channel 1).

void RESTServer::getShutterStatus(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	std::string resp = _shutter.statusJson() + "\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
}

void RESTServer::shutterOpen(const Rest::Request &request, Http::ResponseWriter response)
{
	std::string ch = request.hasParam(":channel") ? request.param(":channel").as<std::string>() : "1";
	bool ok = _shutter.open(ch);
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, ok ? "ok\n" : "error\n");
}

void RESTServer::shutterClose(const Rest::Request &request, Http::ResponseWriter response)
{
	std::string ch = request.hasParam(":channel") ? request.param(":channel").as<std::string>() : "1";
	bool ok = _shutter.close(ch);
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, ok ? "ok\n" : "error\n");
}

void RESTServer::shutterHalt(const Rest::Request &request, Http::ResponseWriter response)
{
	std::string ch = request.hasParam(":channel") ? request.param(":channel").as<std::string>() : "1";
	bool ok = _shutter.halt(ch);
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, ok ? "ok\n" : "error\n");
}

void RESTServer::getCaptureRes(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	std::string resp = std::to_string(_ambiPi->getCaptureWidth()) + "x"
	                 + std::to_string(_ambiPi->getCaptureHeight()) + "\n";
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, resp);
}

void RESTServer::setCaptureRes(const Rest::Request &request, Http::ResponseWriter response)
{
	int w = request.param(":w").as<int>();
	int h = request.param(":h").as<int>();
	_ambiPi->setCaptureRes(w, h);
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, "ok\n");
}

// JMGO power via the Android TV Remote v2 protocol (atvremote.cpp). Needs a
// one-time pairing: run `sudo ambipi --pair-beamer` on the Pi and enter the code
// shown on the projector. WAKEUP=on, SLEEP=off — works from standby (the remote
// service stays reachable, unlike the flaky ADB-over-network).
void RESTServer::beamerOff(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	bool ok = _atv.powerOff();
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, ok ? "ok\n" : "error\n");
}

void RESTServer::beamerOn(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	bool ok = _atv.powerOn();
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, ok ? "ok\n" : "error\n");
}

// Beamer media/volume keys via the Android TV Remote (generic key inject).
// Android keycodes: VOLUME_UP=24, VOLUME_DOWN=25, VOLUME_MUTE=164,
// MEDIA_PLAY_PAUSE=85.
void RESTServer::beamerVolUp(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	bool ok = _atv.sendKey(24);
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, ok ? "ok\n" : "error\n");
}

void RESTServer::beamerVolDown(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	bool ok = _atv.sendKey(25);
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, ok ? "ok\n" : "error\n");
}

void RESTServer::beamerMute(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	bool ok = _atv.sendKey(164);
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, ok ? "ok\n" : "error\n");
}

void RESTServer::beamerPlayPause(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	bool ok = _atv.sendKey(85);
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, ok ? "ok\n" : "error\n");
}

// --- AppleTV power (routed through NodeRED) ---------------------------------
// pyatv + the AppleTV pairing live on the NodeRED host (garagecache), not on
// this Pi, so ambipi can't drive the AppleTV directly. We trigger the existing
// flow's inject nodes ("Apple TV An"=atvx_on / "Apple TV Aus"=atvx_off) via the
// NodeRED Admin API (POST /inject/:id). Server-to-server, so no browser CORS.
static const char* NODERED_HOST = "192.168.178.11";   // garagecache.local
static const char* NODERED_PORT = "1880";

// Minimal HTTP/1.1 POST with empty body; returns true on a 2xx status line.
static bool nodeRedInject(const std::string& injectId)
{
	struct addrinfo hints; memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
	struct addrinfo* res = nullptr;
	if (getaddrinfo(NODERED_HOST, NODERED_PORT, &hints, &res) != 0 || !res) return false;
	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) { freeaddrinfo(res); return false; }
	struct timeval tv; tv.tv_sec = 5; tv.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	bool ok = false;
	if (connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
		std::string req = "POST /inject/" + injectId + " HTTP/1.1\r\n"
			"Host: " + NODERED_HOST + "\r\n"
			"Content-Length: 0\r\n"
			"Connection: close\r\n\r\n";
		if (::send(fd, req.data(), req.size(), 0) == (ssize_t)req.size()) {
			char buf[64]; ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
			if (n > 0) { buf[n] = 0; ok = (strncmp(buf + 8, " 2", 2) == 0); }  // "HTTP/1.x 2xx"
		}
	}
	::close(fd); freeaddrinfo(res);
	return ok;
}

void RESTServer::appleTvOn(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	bool ok = nodeRedInject("atvx_on");
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, ok ? "ok\n" : "error\n");
}

void RESTServer::appleTvOff(const Rest::Request &request, Http::ResponseWriter response)
{
	(void) request;
	bool ok = nodeRedInject("atvx_off");
	response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
	response.send(Http::Code::Ok, ok ? "ok\n" : "error\n");
}

void RESTServer::setDisplay(const Rest::Request &request, Http::ResponseWriter response)
{
	bool enabled = request.param(":enabled").as<bool>();
	std::string resp = enabled ? "true\n" : "false\n";
	response.send(Http::Code::Ok, resp);
	_ambiPi->setEnableDisplayVideo(enabled);
}

void RESTServer::setGamingTable(const Rest::Request &request, Http::ResponseWriter response)
{
	bool enabled = request.param(":enabled").as<bool>();
	std::string resp = enabled ? "true\n" : "false\n";
	response.send(Http::Code::Ok, resp);
	_ambiPi->setEnableGamingTable(enabled);
}

void RESTServer::getDisplay(const Rest::Request &request, Http::ResponseWriter response)
{
	std::string resp = _ambiPi->getEnableDisplayVideo() ? "true\n" : "false\n";
	response.send(Http::Code::Ok, resp);
}

void RESTServer::getGamingTable(const Rest::Request &request, Http::ResponseWriter response)
{
	std::string resp = _ambiPi->getEnableGamingTable() ? "true\n" : "false\n";
	response.send(Http::Code::Ok, resp);
}
