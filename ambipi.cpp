#include "ambipi.h"
#include "unistd.h"
#include <array>
#include <math.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <cstring>

#include "rpi_ws281x/ws2811.h"
#include <nlohmann/json.hpp>


// 30 leds/meter

#define LEDS_TOP	(60)
#define LEDS_BOTTOM	(60)
#define LEDS_LEFT	(34)
#define LEDS_RIGHT	(34)

#define TARGET_FREQ             WS2811_TARGET_FREQ
#define GPIO_PIN1               12
#define GPIO_PIN2               13
#define WS2811_DMA              10
#define MAX_BRIGHTNESS		255

// --- Network targets ---------------------------------------------------------
// All LAN endpoints live here and can be overridden at runtime via
// /etc/ambipi/config.json (see loadNetworkConfig()). If that file is
// missing or unparsable, these built-in defaults are used unchanged.
struct NetConfig {
    // 32x32 "KDP" display
#ifdef DEVEL
    std::string displayServer = "127.0.0.1";
#else
    std::string displayServer = "192.168.178.46";
#endif
    int displayPort = 14000;
    int displayPrio = 0x82;
    // GameWall DDP boards
    std::string ddpHostRight = "192.168.178.146"; // shelves 1..24
    std::string ddpHostLeft  = "192.168.178.185"; // shelves 25..40
    int         ddpPort      = 4048;
    // Gaming table (WLED DNRGB)
    std::string tableHost = "192.168.178.150";
    std::string tablePort = "21324";
    // WiZ bulbs
    int                      wizPort = 38899;
    std::vector<std::string> wizIps  = { "192.168.178.80", "192.168.178.87",
                                         "192.168.178.50", "192.168.178.53" };
    std::string wizLeftQuarterIp  = "192.168.178.109";
    std::string wizRightQuarterIp = "192.168.178.127";
};
static NetConfig g_net;
static constexpr uint8_t DDP_FLAG_VERSION1  = 0x40; // version=1 (bits 6..7)
static constexpr uint8_t DDP_FLAG_PUSH      = 0x01; // push frame immediately
static constexpr uint8_t DDP_FLAGS          = DDP_FLAG_VERSION1 | DDP_FLAG_PUSH;
static constexpr uint8_t DDP_DATATYPE_RGB   = 0x00; // 24-bit RGB
static constexpr size_t  DDP_HEADER_LEN     = 10;

struct Segment { int startIndex; int count; };
using Shelf = std::vector<Segment>;

// --- Dynamic shelves loaded from JSON ---
static std::vector<Shelf> G_SHELVES; // first 40 shelves
static bool G_LOADED = false;

static bool loadShelvesFromJson()
{
    if (G_LOADED) return true;
    const char* path = "/usr/share/ambipi/shelves.json";
    std::ifstream ifs(path);
    if (!ifs) { std::cerr << "[ERROR] Cannot open shelves.json: " << path << "\n"; return false; }
    try {
        nlohmann::json j; ifs >> j;
        if (!j.is_array()) { std::cerr << "[ERROR] shelves.json is not an array\n"; return false; }
        G_SHELVES.clear();
        size_t take = std::min<size_t>(40, j.size());
        G_SHELVES.reserve(take);
        for (size_t i = 0; i < take; ++i) {
            const auto& item = j[i];
            Shelf s;
            if (item.contains("segments") && item["segments"].is_array()) {
                for (const auto& seg : item["segments"]) {
                    if (!seg.contains("startIndex") || !seg.contains("count")) continue;
                    Segment sg{ seg["startIndex"].get<int>(), seg["count"].get<int>() };
                    if (sg.count > 0) s.push_back(sg);
                }
            }
            G_SHELVES.push_back(std::move(s));
        }
        G_LOADED = true;
        std::cerr << "[INFO] Loaded " << G_SHELVES.size() << " shelves from JSON" << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] shelves.json parse error: " << e.what() << "\n"; return false;
    }
}

// Override the built-in network defaults from a JSON file (all keys optional).
static void loadNetworkConfig()
{
    const char* path = "/etc/ambipi/config.json";
    std::ifstream ifs(path);
    if (!ifs) {
        std::cerr << "[INFO] " << path << " not found — using built-in network defaults\n";
        return;
    }
    try {
        nlohmann::json j; ifs >> j;
        g_net.displayServer     = j.value("display_server",       g_net.displayServer);
        g_net.displayPort       = j.value("display_port",         g_net.displayPort);
        g_net.displayPrio       = j.value("display_prio",         g_net.displayPrio);
        g_net.ddpHostRight      = j.value("ddp_host_right",       g_net.ddpHostRight);
        g_net.ddpHostLeft       = j.value("ddp_host_left",        g_net.ddpHostLeft);
        g_net.ddpPort           = j.value("ddp_port",             g_net.ddpPort);
        g_net.tableHost         = j.value("table_host",           g_net.tableHost);
        g_net.tablePort         = j.value("table_port",           g_net.tablePort);
        g_net.wizPort           = j.value("wiz_port",             g_net.wizPort);
        if (j.contains("wiz_ips") && j["wiz_ips"].is_array())
            g_net.wizIps = j["wiz_ips"].get<std::vector<std::string>>();
        g_net.wizLeftQuarterIp  = j.value("wiz_left_quarter_ip",  g_net.wizLeftQuarterIp);
        g_net.wizRightQuarterIp = j.value("wiz_right_quarter_ip", g_net.wizRightQuarterIp);
        std::cerr << "[INFO] Loaded network config from " << path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] config.json parse error: " << e.what() << " — using defaults\n";
    }
}



std::vector<uint8_t> buildGammaLUT(float gamma_factor) {
    std::vector<uint8_t> lut(256);
    for (int i = 0; i < 256; ++i) {
        float normalized = i / 255.0f;
        float corrected = std::pow(normalized, gamma_factor);
        int out = static_cast<int>(corrected * 255.0f + 0.5f);
        if (out < 0) out = 0;
        if (out > 255) out = 255;
        lut[i] = static_cast<uint8_t>(out);
    }
    return lut;
}


AmbiPi::AmbiPi() : _mode(Off), _alpha(0.5), _gamma(0), _enableCropping(false)
{
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
	uint8_t a = 2;
	_colorsL = cv::Mat(LEDS_LEFT - a, 1,   CV_8UC3, cv::Scalar(b, g, r));
	_colorsT = cv::Mat(1, LEDS_TOP - a,    CV_8UC3, cv::Scalar(b, g, r));
	_colorsB = cv::Mat(1, LEDS_BOTTOM - a, CV_8UC3, cv::Scalar(b, g, r));
	_colorsR = cv::Mat(LEDS_RIGHT - a, 1,  CV_8UC3, cv::Scalar(b, g, r));
	clearLastFrame(0,0,0);
	_enableDisplayVideo = false;
	_enableGamingTable = false;
	_enableGameWallAmbilight = false;
	_swapRB = false;
}

AmbiPi::~AmbiPi()
{
	if (_ws2811) {
		ws2811_fini(_ws2811);
		free(_ws2811);
	}
}

void AmbiPi::clearLastFrame(uint8_t r,uint8_t g,uint8_t b)
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	setLastFrame(cv::Mat(480, 720, CV_8UC3, cv::Scalar(b,g,r)));
	_cropRect = cv::Rect(0, 0, 720, 480);
}

bool AmbiPi::init(double gamma)
{
	loadNetworkConfig();
	loadSettings();
	_ws2811 = (ws2811_t *) malloc(sizeof(ws2811_t));
	memset(_ws2811, 0, sizeof(ws2811_t));

	_ws2811->freq			= WS2811_TARGET_FREQ;
	_ws2811->dmanum			= WS2811_DMA;
	_ws2811->channel[0].gpionum	= GPIO_PIN1;
	_ws2811->channel[0].count	= LEDS_LEFT +  LEDS_TOP;// + LEDS_RIGHT + LEDS_BOTTOM;
	_ws2811->channel[0].strip_type	= WS2812_STRIP;
	_ws2811->channel[0].brightness	= MAX_BRIGHTNESS;

	_ws2811->channel[1].gpionum	= GPIO_PIN2;
	_ws2811->channel[1].count	= LEDS_BOTTOM + LEDS_RIGHT;
	_ws2811->channel[1].strip_type	= WS2812_STRIP;
	_ws2811->channel[1].brightness	= MAX_BRIGHTNESS;

#ifdef _GUI_
	_ws2811->channel[0].leds = (ws2811_led_t*) malloc(sizeof(ws2811_led_t) * _ws2811->channel[0].count);
	_ws2811->channel[1].leds = (ws2811_led_t*) malloc(sizeof(ws2811_led_t) * _ws2811->channel[1].count);
	memset(_ws2811->channel[0].leds, 0, sizeof(ws2811_led_t) * _ws2811->channel[0].count);
	memset(_ws2811->channel[1].leds, 0, sizeof(ws2811_led_t) * _ws2811->channel[1].count);
	ws2811_return_t ret = WS2811_SUCCESS;
#else
	ws2811_return_t ret = ws2811_init(_ws2811);
#endif
	if (ret != WS2811_SUCCESS) {
		fprintf(stderr, "Error: %s\n", ws2811_get_return_t_str(ret));
		free(_ws2811);
		return false;
	}

	setGamma(gamma);
	return true;
}

void AmbiPi::setUpdateCropRect(bool cropping)
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	_enableCropping = cropping;
}

bool AmbiPi::croppingEnabled() const
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	return _enableCropping;
}

void AmbiPi::setBrightness(uint8_t bri)
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	_ws2811->channel[0].brightness	= bri;
	_ws2811->channel[1].brightness	= bri;
}

uint8_t AmbiPi::brightness() const
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	return _ws2811->channel[0].brightness;
}

void AmbiPi::setAlpha(double alpha)
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	_alpha = alpha;
}

double AmbiPi::alpha() const
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	return _alpha;
}


void AmbiPi::setGamma(double gamma)
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	_gamma = gamma;
	if (gamma != 0) {
		ws2811_set_custom_gamma_factor(_ws2811, gamma);
		_lut = buildGammaLUT(gamma); 
	}
}

double AmbiPi::gamma() const
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	return _gamma;
}

void AmbiPi::clear()
{
	setColor(0,0,0);
}

void AmbiPi::setColor(uint8_t r, uint8_t g, uint8_t  b)
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	setColorLeft(r,g,b);
	setColorTop(r,g,b);
	setColorBottom(r,g,b);
	setColorRight(r,g,b);
}

void AmbiPi::setColor(int idx, uint8_t r, uint8_t g, uint8_t  b)
{
	if (idx<LEDS_LEFT) {
		setColorLeft(LEDS_LEFT-idx-1,r,g,b);
	} else if (idx<LEDS_LEFT+LEDS_TOP) {
		setColorTop(idx-LEDS_LEFT,r,g,b);
	} else if (idx<LEDS_LEFT+LEDS_TOP+LEDS_RIGHT) {
		setColorRight(idx-LEDS_LEFT-LEDS_TOP,r,g,b);
	} else {
		setColorBottom(LEDS_BOTTOM-(idx-LEDS_LEFT-LEDS_TOP-LEDS_RIGHT)-1,r,g,b);
	}
}

void AmbiPi::setColorLeft(uint8_t r, uint8_t g, uint8_t  b)
{
	for (int i=0; i < LEDS_LEFT; i++) {
		_ws2811->channel[0].leds[i] = ((r & 0x0ff) << 16) | ((g & 0x0ff) << 8) | (b & 0x0ff);
	}
}




void AmbiPi::setColorTop(uint8_t r, uint8_t g, uint8_t  b)
{
	for (int i=0; i < LEDS_TOP; i++) {
		_ws2811->channel[0].leds[LEDS_LEFT+i] = ((r & 0x0ff) << 16) | ((g & 0x0ff) << 8) | (b & 0x0ff);
	}	
}

void AmbiPi::setColorBottom(uint8_t r, uint8_t g, uint8_t  b)
{
	for (int i=0; i < LEDS_BOTTOM; i++) {
		_ws2811->channel[1].leds[i] = ((r & 0x0ff) << 16) | ((g & 0x0ff) << 8) | (b & 0x0ff);
	}
}

void AmbiPi::setColorRight(uint8_t r, uint8_t g, uint8_t  b)
{
	for (int i=0; i < LEDS_RIGHT; i++) {
		_ws2811->channel[1].leds[LEDS_BOTTOM+i] = ((r & 0x0ff) << 16) | ((g & 0x0ff) << 8) | (b & 0x0ff);
	}
}

void AmbiPi::setColorLeft(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
	assert(idx>=0 && idx<LEDS_LEFT);
	_ws2811->channel[0].leds[LEDS_LEFT-idx-1] = ((r & 0x0ff) << 16) | ((g & 0x0ff) << 8) | (b & 0x0ff);
}

void AmbiPi::setColorTop(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
	assert(idx>=0 && idx<LEDS_TOP);
	_ws2811->channel[0].leds[LEDS_LEFT+idx] = ((r & 0x0ff) << 16) | ((g & 0x0ff) << 8) | (b & 0x0ff);
}

void AmbiPi::getColorTop(uint8_t idx, uint8_t* r, uint8_t* g, uint8_t* b)
{
	assert(idx>=0 && idx<LEDS_TOP);
	uint32_t col = _ws2811->channel[0].leds[LEDS_LEFT+idx];
	*r = (col >> 16) & 0xff;
	*g = (col >>  8) & 0xff;
	*b = (col >>  0) & 0xff;
}

void AmbiPi::setColorBottom(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
	assert(idx>=0 && idx<LEDS_BOTTOM);
	_ws2811->channel[1].leds[idx] = ((r & 0x0ff) << 16) | ((g & 0x0ff) << 8) | (b & 0x0ff);
}

void AmbiPi::setColorRight(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
	assert(idx>=0 && idx<LEDS_RIGHT);
	_ws2811->channel[1].leds[LEDS_BOTTOM+LEDS_RIGHT-idx-1] = ((r & 0x0ff) << 16) | ((g & 0x0ff) << 8) | (b & 0x0ff);
}

void AmbiPi::getRainbowColor(int pos, uint8_t& r, uint8_t& g, uint8_t& b)
{
	pos = pos % 256;
	if (pos<85) {
		r = pos*3;
		g = 255-pos*3;
		b = 0;
	} else if (pos<170) {
		pos -= 85;
		r = 255-pos*3;
		g = 0;
		b = pos*3;
	} else {
		pos -= 170;
		r = 0;
		g = pos*3;
		b = 255-pos*3;
	}
} 

int AmbiPi::drawTestPattern(int i)
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	uint8_t r,g,b;
	uint8_t pos = (i/10);
	getRainbowColor(pos, r, g, b);
	setColorLeft  (r, g, b);
	getRainbowColor(pos+64, r, g, b);
	setColorBottom(r, g, b);
	getRainbowColor(pos+128, r, g, b);
	setColorRight (r, g, b);
	getRainbowColor(pos+192, r, g, b);
	setColorTop   (r, g, b);

	// setColor(i % ledCount(), 255, 255, 255);
	return 5;
}

int AmbiPi::ledCount() const
{
	return LEDS_LEFT + LEDS_TOP + LEDS_RIGHT + LEDS_BOTTOM;
}

int AmbiPi::knightrider(int cnt)
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	// clear();
	int p = (1+sin(cnt*M_PI/90.)) * 0.5 * (LEDS_TOP-1);
	
	double a = 0.9;
	uint8_t r,g,b;
	for (int i=0; i<LEDS_TOP; i++) {
		getColorTop(i, &r, &g, &b);
		r *= a;
		g *= a;
		b *= a;
		setColorTop(i, r, g, b);
	}
	setColorTop(p, 0xff, 0, 0);
	
	return 25;
}

int AmbiPi::vegas(int cnt)
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	int c = ledCount();
	uint8_t r,g,b;
	for (int i = 0; i<c; i++) {
		if ((cnt+i)%4 < 2) {
			
			if (i%2 == 0) {
				r = 255;
				g = 192;
				b = 0;
			} else {
				r = 255;
				g = 0;
				b = 0;
			}
		} else {
			r = 0;
			g = 0;
			b = 0;
		}
		setColor(i,r,g,b);
	}
	return 100;
}

int AmbiPi::goal(int cnt, bool leftSide)
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	// BottomMid to Left To Top to TopMid
	uint8_t r,g,b, pos;
	int lc = ledCount();
	int lc2 = lc/2;
	int cc = cnt % lc2;
	
	if (leftSide) {
		r = 0;
		g = 0;
		b = 255;
		pos = (cc - LEDS_BOTTOM/2 + lc) % lc;
	} else {
		r = 255;
		g = 0;
		b = 0;
		pos = ((lc-cc) - LEDS_BOTTOM/2 + lc) % lc;
	}
	fadeColors(0.97);
	setColor(pos, r, g, b);
	return 10;
}

void AmbiPi::fadeColors(float pct)
{
	uint8_t r,g,b;
	for (int j=0; j<2; j++) {
		for (int i=0; i<_ws2811->channel[j].count; i++) {
			uint32_t col = _ws2811->channel[j].leds[i];
			r = (col >> 16) & 0xff;
			g = (col >>  8) & 0xff;
			b = (col >>  0) & 0xff;
			r *= pct;
			g *= pct;
			b *= pct;
			_ws2811->channel[j].leds[i] = ((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff);
		}
	}
}

int AmbiPi::rainbow(int cnt)
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	int c = ledCount();
	uint8_t r,g,b;
	for (int i = 0; i<c; i++) {
		uint8_t pos = (255*i / (c-1) + cnt);
		getRainbowColor(pos, r, g, b);
		setColor(i, r, g, b);
	}
	return 25;
}

void AmbiPi::unpackRgb(uint32_t packed, uint8_t &r, uint8_t &g, uint8_t &b)
{
    r = (packed >> 16) & 0xFF;
    g = (packed >> 8) & 0xFF;
    b = packed & 0xFF;
    r = _lut[r];
    g = _lut[g];
    b = _lut[b];
}

static inline uint8_t lerp8(uint8_t a, uint8_t b, float t)
{
    return static_cast<uint8_t>(std::round(a + (b - a) * t));
}

std::vector<uint8_t> stretchAndInterpolate(const std::vector<uint8_t>& input, size_t target_leds)
{
    size_t source_leds = input.size() / 3;
    std::vector<uint8_t> output;
    if (source_leds == 0 || target_leds == 0) return output;
    output.reserve(target_leds * 3);

    for (size_t i = 0; i < target_leds; ++i) {
        // Position im Raum der Quelle: von 0..source_leds-1
        float pos = (source_leds == 1) ? 0.0f : (static_cast<float>(i) * (source_leds - 1)) / (target_leds - 1);
        size_t idx_low = static_cast<size_t>(std::floor(pos));
        size_t idx_high = static_cast<size_t>(std::min<float>(source_leds - 1, std::ceil(pos)));
        float t = pos - idx_low;

        uint8_t r0 = input[3 * idx_low + 0];
        uint8_t g0 = input[3 * idx_low + 1];
        uint8_t b0 = input[3 * idx_low + 2];
        uint8_t r1 = input[3 * idx_high + 0];
        uint8_t g1 = input[3 * idx_high + 1];
        uint8_t b1 = input[3 * idx_high + 2];

        uint8_t r = lerp8(r0, r1, t);
        uint8_t g = lerp8(g0, g1, t);
        uint8_t b = lerp8(b0, b1, t);

        output.push_back(r);
        output.push_back(g);
        output.push_back(b);
    }
    return output;
}

bool sendWledDnRgbRange(const char* host, const char* port, uint16_t startIndex, const std::vector<uint8_t>& rgb_values, uint8_t timeout_seconds = 1)
{
    if (rgb_values.size() % 3 != 0) {
        std::cerr << "[ERROR] rgb_values Länge muss ein Vielfaches von 3 sein.\n";
        return false;
    }
    size_t ledCount = rgb_values.size() / 3;
    if (ledCount == 0) {
        std::cerr << "[WARN] Keine LEDs zu senden.\n";
        return true;
    }

    // Begrenze Größe: typisches UDP-Limit im LAN, splitte ansonsten
    const size_t max_payload = 512; // sicherer Bereich; 4 + 2 + 3*LEDs sollte <= 512 sein
    size_t needed = 1 + 1 + 2 + 3 * ledCount; // mode + timeout + start hi/lo + RGBs
    if (needed > max_payload) {
        std::cerr << "[INFO] Paket zu groß (" << needed << " Bytes), in Chunks aufteilen.\n";
        // Teile in kleinere Stücke auf, z.B.  (max_leds_per = floor((max_payload -4)/3))
        size_t max_leds_per = (max_payload - 4) / 3;
        for (size_t offset = 0; offset < ledCount; offset += max_leds_per) {
            size_t chunk = std::min(max_leds_per, ledCount - offset);
            std::vector<uint8_t> sub(rgb_values.begin() + offset * 3,
                                     rgb_values.begin() + (offset + chunk) * 3);
            if (!sendWledDnRgbRange(host, port, startIndex + offset, sub, timeout_seconds)) {
                return false;
            }
        }
        return true;
    }

    std::vector<uint8_t> packet;
    packet.reserve(needed);
    packet.push_back(4); // DNRGB mode
    packet.push_back(timeout_seconds);
    packet.push_back(static_cast<uint8_t>((startIndex >> 8) & 0xFF));
    packet.push_back(static_cast<uint8_t>(startIndex & 0xFF));
    packet.insert(packet.end(), rgb_values.begin(), rgb_values.end());

    // Resolve the destination once and cache it: getaddrinfo() per frame is
    // expensive and was previously called on every render tick.
    static std::mutex s_addrCacheMutex;
    static std::unordered_map<std::string, std::pair<sockaddr_storage, socklen_t>> s_addrCache;

    sockaddr_storage dest{};
    socklen_t destLen = 0;
    {
        const std::string key = std::string(host) + ":" + port;
        std::lock_guard<std::mutex> lk(s_addrCacheMutex);
        auto it = s_addrCache.find(key);
        if (it != s_addrCache.end()) {
            dest    = it->second.first;
            destLen = it->second.second;
        } else {
            struct addrinfo hints{};
            struct addrinfo* res = nullptr;
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_DGRAM;
            int rc = getaddrinfo(host, port, &hints, &res);
            if (rc != 0 || res == nullptr) {
                std::cerr << "[ERROR] getaddrinfo fehlgeschlagen: " << gai_strerror(rc) << "\n";
                if (res) freeaddrinfo(res);
                return false;
            }
            std::memcpy(&dest, res->ai_addr, res->ai_addrlen);
            destLen = static_cast<socklen_t>(res->ai_addrlen);
            freeaddrinfo(res);
            s_addrCache.emplace(key, std::make_pair(dest, destLen));
        }
    }

    int sock = socket(dest.ss_family, SOCK_DGRAM, 0);
    if (sock == -1) {
        std::cerr << "[ERROR] Konnte keinen Socket erzeugen.\n";
        return false;
    }

    ssize_t sent = sendto(sock, packet.data(), packet.size(), 0,
                          reinterpret_cast<sockaddr*>(&dest), destLen);
    if (sent != (ssize_t)packet.size()) {
        std::cerr << "[ERROR] sendto fehlgeschlagen: " << strerror(errno)
                  << " gesendet: " << sent << " von " << packet.size() << "\n";
        close(sock);
        return false;
    }

    close(sock);
    return true;
}

void AmbiPi::render()
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
#ifdef _GUI_
	return;
#endif
	// ws2811_wait(_ws2811);
	ws2811_render(_ws2811);
if (_enableGamingTable) {
    const char* host = g_net.tableHost.c_str();
    const char* port = g_net.tablePort.c_str();
#if 0
    {
    // Schrank
    	const char* host = "192.168.178.146";
        uint16_t startIndex = 0;
        std::vector<uint8_t> rgbRight1;
        std::vector<uint8_t> rgbRight2;
        rgbRight1.reserve(3 * LEDS_RIGHT);
        rgbRight2.reserve(3 * LEDS_RIGHT);
        for (int i = 0; i < LEDS_RIGHT; ++i) {
{            
        	uint32_t pix = _ws2811->channel[1].leds[LEDS_BOTTOM + i];
        	uint8_t r, g, b;
        	unpackRgb(pix, r, g, b);
       	        rgbRight2.push_back(r);
                rgbRight2.push_back(g);
                rgbRight2.push_back(b);
}
{            
        	uint32_t pix = _ws2811->channel[1].leds[LEDS_BOTTOM + LEDS_RIGHT-1-i];
        	uint8_t r, g, b;
        	unpackRgb(pix, r, g, b);
       	        rgbRight1.push_back(r);
                rgbRight1.push_back(g);
                rgbRight1.push_back(b);
}
        }
        
        rgbRight1 = stretchAndInterpolate(rgbRight1, 50);
        rgbRight2 = stretchAndInterpolate(rgbRight2, 50);

        sendWledDnRgbRange(host, port,  1, rgbRight1);
        sendWledDnRgbRange(host, port, 59, rgbRight2);

        sendWledDnRgbRange(host, port,  1+234, rgbRight1);
        sendWledDnRgbRange(host, port, 59+234, rgbRight2);
//        sendWledDnRgbRange(host, port, 1+117, rgbRight);
//        sendWledDnRgbRange(host, port, 1+234, rgbRight);
    }
    return;
#endif    
    // --- Top LEDs: aus channel[0].leds[LEDS_LEFT .. LEDS_LEFT+LEDS_TOP)
    {
        uint16_t startIndex = 0;
        std::vector<uint8_t> rgbTop;
        rgbTop.reserve(3 * LEDS_TOP);
        for (int i = 0; i < LEDS_TOP; ++i) {
            uint32_t pix = _ws2811->channel[0].leds[LEDS_LEFT + i];
            uint8_t r, g, b;
            unpackRgb(pix, r, g, b);
            rgbTop.push_back(r);
            rgbTop.push_back(g);
            rgbTop.push_back(b);
        }
        
        rgbTop = stretchAndInterpolate(rgbTop, 86);

        bool okTop = sendWledDnRgbRange(host, port, startIndex, rgbTop);
        if (!okTop) {
            std::cerr << "[WARN] DNRGB Top senden fehlgeschlagen.\n";
        }
    }

    // --- Bottom LEDs: aus channel[1].leds[0 .. LEDS_BOTTOM)
    {
        uint16_t startIndex = 86;
        std::vector<uint8_t> rgbBottom;
        rgbBottom.reserve(3 * LEDS_BOTTOM);
        for (int i = LEDS_BOTTOM - 1; i >= 0; --i) {
            uint32_t pix = _ws2811->channel[1].leds[i];
            uint8_t r, g, b;
            unpackRgb(pix, r, g, b);
            rgbBottom.push_back(r);
            rgbBottom.push_back(g);
            rgbBottom.push_back(b);
        }

        rgbBottom = stretchAndInterpolate(rgbBottom, 86);
        bool okBottom = sendWledDnRgbRange(host, port, startIndex, rgbBottom);
        if (!okBottom) {
            std::cerr << "[WARN] DNRGB Bottom senden fehlgeschlagen.\n";
        }
    }
}
}

cv::Mat AmbiPi::getDebugFrame(cv::Mat frame) const
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	int top   = LEDS_TOP     - 2;
	int left  = LEDS_LEFT    - 2;

	double dw = frame.cols / (double) (LEDS_TOP  - 2);
	double dh = frame.rows / (double) (LEDS_LEFT - 2);

	for (int x=0;x<top;x++) {
		cv::rectangle(frame, cv::Rect(x*dw,0,dw,dh), cv::Scalar(128,128,128),1);
		cv::rectangle(frame, cv::Rect(x*dw,(left-1)*dh,dw,dh), cv::Scalar(128,128,128),1);

	}
	for (int y=0;y<left;y++) {
		cv::rectangle(frame, cv::Rect(0,y*dh,dw,dh), cv::Scalar(128,128,128),1);
		cv::rectangle(frame, cv::Rect((top-1)*dw,y*dh,dw,dh), cv::Scalar(128,128,128),1);

	}

	const int border = 50;
	cv::Mat out(frame.rows+2*border, frame.cols+2*border, CV_8UC3, cv::Scalar(0, 0, 0));

	int ox = (out.cols - frame.cols) / 2;
	int oy = (out.rows - frame.rows) / 2;

	double fw = out.cols / (double) frame.cols;
	double fh = out.rows / (double) frame.rows;

	double dwT = frame.cols / (double) (LEDS_TOP    - 2);
	double dwB = frame.cols / (double) (LEDS_BOTTOM - 2);
	double dhL = frame.rows / (double) (LEDS_LEFT   - 2);
	double dhR = frame.rows / (double) (LEDS_RIGHT  - 2);

	int  oxx = ox - dwT;
	int  oyy = oy - dhL;

	// cv::blur(out, out, cv::Size(fw*(dwT+dwB)/2, fh*(dhL+dhR)/2));
	frame.copyTo(out(cv::Rect(ox, oy, frame.cols, frame.rows)));

	for (int x=0; x < LEDS_TOP; x++) {
		uint32_t col = _ws2811->channel[0].leds[LEDS_LEFT+x];
		cv::Vec3b colorT = cv::Vec3b((col>>0) & 0xff, (col>>8) & 0xff , (col>>16) & 0xff);
		cv::rectangle(out, cv::Rect(oxx + (x)*dwT,oyy-dhL,dwT,dhL), colorT, -1);
		cv::rectangle(out, cv::Rect(oxx + (x)*dwT,oyy-dhL,dwT,dhL), cv::Scalar(128,128,128), 1);
		cv::putText(out, std::to_string(LEDS_LEFT+x), cv::Point(oxx+(x+0.125)*dwT,oyy-1.25*dhL), cv::FONT_HERSHEY_COMPLEX_SMALL, 0.5, cv::Scalar(128,128,128), 1, cv::LINE_AA);
	}
	for (int x=0; x < LEDS_BOTTOM; x++) {
		uint32_t col = _ws2811->channel[1].leds[x];
		cv::Vec3b colorB = cv::Vec3b((col>>0) & 0xff, (col>>8) & 0xff , (col>>16) & 0xff);
		cv::rectangle(out, cv::Rect(oxx + (x)*dwT,oyy+LEDS_LEFT*dhL,dwT,dhL), colorB, -1);
		cv::rectangle(out, cv::Rect(oxx + (x)*dwT,oyy+LEDS_LEFT*dhL,dwT,dhL), cv::Scalar(128,128,128), 1);
		cv::putText(out, std::to_string(x), cv::Point(oxx+(x+0.125)*dwT,oyy+(LEDS_LEFT+1.5)*dhL + 2), cv::FONT_HERSHEY_COMPLEX_SMALL, 0.5, cv::Scalar(128,128,128), 1, cv::LINE_AA);
	}
	for (int y=0; y < LEDS_LEFT; y++) {
		uint32_t col = _ws2811->channel[0].leds[LEDS_LEFT-y-1];
		cv::Vec3b colorL = cv::Vec3b((col>>0) & 0xff, (col>>8) & 0xff , (col>>16) & 0xff);
		cv::rectangle(out, cv::Rect(oxx-dwT, oyy + (y)*dhL, dwT, dhL), colorL, -1);
		cv::rectangle(out, cv::Rect(oxx-dwT, oyy + (y)*dhL, dwT, dhL), cv::Scalar(128,128,128), 1);
		cv::putText(out, std::to_string(LEDS_LEFT-y-1), cv::Point(oxx-2*dwT - 4, oyy + (y+0.75)*dhL), cv::FONT_HERSHEY_COMPLEX_SMALL, 0.5, cv::Scalar(128,128,128), 1, cv::LINE_AA);
	}
	for (int y=0; y < LEDS_RIGHT; y++) {
		uint32_t col = _ws2811->channel[1].leds[LEDS_BOTTOM+LEDS_RIGHT-y-1];
		cv::Vec3b colorL = cv::Vec3b((col>>0) & 0xff, (col>>8) & 0xff , (col>>16) & 0xff);
		cv::rectangle(out, cv::Rect(oxx+(LEDS_TOP*dwT), oyy + (y)*dhL, dwT, dhL), colorL, -1);
		cv::rectangle(out, cv::Rect(oxx+(LEDS_TOP*dwT), oyy + (y)*dhL, dwT, dhL), cv::Scalar(128,128,128), 1);
		cv::putText(out, std::to_string(LEDS_BOTTOM+LEDS_RIGHT-y-1), cv::Point(oxx+((LEDS_TOP+1.5)*dwT), oyy + (y+0.75)*dhL), cv::FONT_HERSHEY_COMPLEX_SMALL, 0.5, cv::Scalar(128,128,128), 1, cv::LINE_AA);
	}
	return out;
}

#ifdef _GUI_
void AmbiPi::drawGUI(cv::Mat frame)
{
	if (frame.empty()) {
		frame = cv::Mat(1080, 1920, CV_8UC3, cv::Scalar(64,64,64));
	}
	cv::resize(frame, frame, cv::Size(), 0.5, 0.5, cv::INTER_LINEAR);
	cv::Mat out = getDebugFrame(frame);
	cv::imshow("AmbiPi", out);
}
#endif

cv::Mat AmbiPi::createTestImage(int w, int h)
{
	//	uint8_t r = 0xff;
	//	uint8_t g = 0xff;
	//	uint8_t b = 0xff;
	int d = 5;
	cv::Mat frame = cv::Mat(LEDS_LEFT, LEDS_TOP,   CV_8UC3, cv::Scalar(0,0,0));
	for  (int y=0; y<LEDS_LEFT; y++) {
		for (int x=0; x<LEDS_TOP; x++) {
			if ((x/d%2)^(y/d%2)) {
				frame.at<cv::Vec3b>(y,x) = cv::Vec3b(0, 0, 0xff);
			} else {
				frame.at<cv::Vec3b>(y,x) = cv::Vec3b(0xff, 0, 0);
			}
		}
	}
	cv::resize(frame, frame, cv::Size(w,h), 0, 0, cv::INTER_NEAREST);
	// fprintf(stderr, "AmbiPi::createTestImage(%dx%d) -> %dx%d (%dx%d)\n", LEDS_TOP, LEDS_LEFT, frame.cols, frame.rows, w, h);
	// imwrite("/home/pi/checkers.png", frame); exit(0);
	return frame;
}

void AmbiPi::updateCropRect(cv::Mat frame)
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	fprintf(stderr, "updateCropRect(%dx%d)\n", frame.cols, frame.rows);
	int minX = 0;
	int minY = 0;
	int maxX = frame.cols-1;
	int maxY = frame.rows-1;
	
	int i;
	
	cv::Mat gray;
	cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
	
	cv::Canny(gray, gray, 50, 50, 3,  false);

	// gray.convertTo(gray, -1, 8, 0);
	
	for (i=0; i<gray.cols*0.2;i++) {
		if (cv::countNonZero(gray(cv::Rect(i,0,1,gray.rows))) > 0) {
			break;
		}
	}
	minX = i;

	for (i=gray.cols-1; i>gray.cols*0.8;i--) {
		if (cv::countNonZero(gray(cv::Rect(i,0,1,gray.rows))) > 0) {
			break;
		}
	}
	maxX = i;

	for (i=0; i<gray.rows*0.2;i++) {
		if (cv::countNonZero(gray(cv::Rect(0,i,gray.cols,1))) > 0) {
			break;
		}
	}
	minY = i;

	for (i=gray.rows-1; i>gray.rows*0.8;i--) {
		if (cv::countNonZero(gray(cv::Rect(0,i,gray.cols,1))) > 0) {
			break;
		}
	}
	maxY = i;
	
	if (maxX-16 < minX || maxY-16 < minY) { // Reset
		minX = 0;
		minY = 0;
		maxX = frame.cols-1;
		maxY = frame.rows-1;
	}
	
	
	// fprintf(stderr, "cropBorder(%dx%d -> %d,%d %dx%d)\n", frame.cols, frame.rows, minX, minY, maxX-minX+1,maxY-minY+1);
	_cropRect = cv::Rect(minX,minY, maxX-minX+1, maxY-minY+1);
	fprintf(stderr, "updateCropRect(%dx%d) -> %d,%d %dx%d\n", frame.cols, frame.rows, minX,minY, maxX-minX+1, maxY-minY+1);
}


cv::Mat AmbiPi::cropBorders(cv::Mat frame, bool debug) const
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	cv::Mat cropped = frame(_cropRect);
	cv::Mat out;
	
	// debug = false;
	if (!debug) {
		cv::Mat scaled;
		out = cv::Mat(frame.rows, frame.cols, CV_8UC3, cv::Scalar(0,0,0));
		cv::resize(cropped, scaled, cv::Size(_cropRect.width, frame.rows), 0, 0, cv::INTER_NEAREST);
		scaled.copyTo(out(cv::Rect(_cropRect.x, 0, _cropRect.width, frame.rows)));

		cv::resize(cropped, scaled, cv::Size(frame.cols, _cropRect.height), 0, 0, cv::INTER_NEAREST);
		scaled.copyTo(out(cv::Rect(0, _cropRect.y, frame.cols, _cropRect.height)));

		// cv::resize(cropped, out, cv::Size(frame.cols, frame.rows), 0, 0, cv::INTER_NEAREST);
		cropped.copyTo(out(_cropRect));
	} else {
		out = cv::Mat(frame.rows, frame.cols, CV_8UC3, cv::Scalar(64,0,0));
		cropped.copyTo(out(_cropRect));
		// cv::cvtColor(gray, out, cv::COLOR_GRAY2BGR);
		cv::rectangle(out, _cropRect, cv::Scalar(0,0,255));
	}
	return out;
}

void AmbiPi::calculateDisplayFrameFromFrame(cv::Mat frame)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    // Horizontal correction factor (stretch); 1.0 = off.
    // Increase if the input looks horizontally compressed.
    // Tweak this value to your setup (e.g. 1.10 .. 1.40).
    constexpr double kXFix = 1.30;

    // 1) Optional horizontal stretch
    const int w = frame.cols;
    const int h = frame.rows;
    cv::Mat stretched;

    if (std::abs(kXFix - 1.0) < 1e-6) {
        stretched = frame;
    } else {
        const int newW = std::max(1, static_cast<int>(std::lround(w * kXFix)));
        cv::resize(frame, stretched, cv::Size(newW, h), 0, 0, cv::INTER_LANCZOS4);
    }

    // 2) Center-crop to square from the stretched image
    const int sw = stretched.cols;
    const int sh = stretched.rows;
    const int side = std::min(sw, sh);
    const int x = (sw - side) / 2;
    const int y = (sh - side) / 2;

    cv::Mat squareFrame = stretched(cv::Rect(x, y, side, side));

    // 3) Downscale to 32×32 and send (RGB order)
    cv::Mat out32;
    cv::resize(squareFrame, out32, cv::Size(32, 32), 0, 0, cv::INTER_LANCZOS4);

    cv::Mat rgbFrame;
    cv::cvtColor(out32, rgbFrame, cv::COLOR_BGR2RGB);
    sendFullFrame(rgbFrame);
}

// Compute total LEDs we need to cover based on loaded shelves
static int computeStripLen()
{
  if (!G_LOADED) loadShelvesFromJson();
  int maxIdx = 0;
  for (const auto& shelf : G_SHELVES) {
    for (const auto& seg : shelf) {
      maxIdx = std::max(maxIdx, seg.startIndex + seg.count);
    }
  }
  return maxIdx;
}

// Compute total LEDs for a subset of shelves [first..last] (1-based, inclusive)
static int computeStripLenRange(int first, int last)
{
  if (!G_LOADED) loadShelvesFromJson();
  first = std::max(1, first); last = std::min<int>(last, static_cast<int>(G_SHELVES.size()));
  int maxIdx = 0;
  for (int s = first; s <= last; ++s) {
    const auto& shelf = G_SHELVES[s-1];
    for (const auto& seg : shelf) maxIdx = std::max(maxIdx, seg.startIndex + seg.count);
  }
  return maxIdx;
}

// --- Mapping tweak flags: flip axes if the layout feels mirrored ---
static const bool kMirrorRightX = false;  // flip left/right for shelves 1..24
static const bool kMirrorRightY = false;  // flip top/bottom for shelves 1..24
static const bool kMirrorLeftX  = false;  // flip left/right for shelves 25..40 (L)
static const bool kMirrorLeftY  = false;  // flip top/bottom for shelves 25..40 (L)

// Map shelf number (1..40) to (x,y) in a 6×4 grid (both walls sample the same 6-wide image).
// Right board (1..24): row-major in a 6×4 (top→bottom, left→right).
// Left board (25..40): L-formed inside the same 6×4:
// Row0: -- -- -- -- 25 26
// Row1: -- -- -- -- 27 28
// Row2: 29 30 31 32 33 34
// Row3: 35 36 37 38 39 40
static inline void shelfToXY(int shelf, int& x, int& y)
{
  // Right board (1..24): row-major 6×4
  if (shelf >= 1 && shelf <= 24) {
    const int row = (shelf - 1) / 6;   // 0..3
    const int col = (shelf - 1) % 6;   // 0..5
    int xr = col;
    int yr = row;
    if (kMirrorRightX) xr = 5 - xr;
    if (kMirrorRightY) yr = 3 - yr;
    x = xr; y = yr; return;
  }

  // Left board (25..40): L-formed inside the same 6×4
  if (shelf >= 25 && shelf <= 40) {
    int xl = 0, yl = 0;
    switch (shelf) {
      // Row0: -- -- -- -- 25 26
      case 25: yl = 0; xl = 4 + (shelf - 25); break; // x=4
      case 26: yl = 0; xl = 4 + (shelf - 25); break; // x=5

      // Row1: -- -- -- -- 27 28
      case 27: yl = 1; xl = 4 + (shelf - 27); break; // x=4
      case 28: yl = 1; xl = 4 + (shelf - 27); break; // x=5

      // Row2: 29 30 31 32 33 34
      case 29: case 30: case 31: case 32: case 33: case 34:
        yl = 2; xl = (shelf - 29); break; // x=0..5

      // Row3: 35 36 37 38 39 40
      case 35: case 36: case 37: case 38: case 39: case 40:
        yl = 3; xl = (shelf - 35); break; // x=0..5

      default:
        xl = 0; yl = 0; break; // safety fallback
    }
    if (kMirrorLeftX) xl = 5 - xl;
    if (kMirrorLeftY) yl = 3 - yl;
    x = xl; y = yl; return;
  }

  // Fallback
  x = 0; y = 0;
}

// Build two LED buffers (right board = shelves 1..24, left board = shelves 25..40)
static void buildLedBuffersFromFrame(const cv::Mat& targetFrame,
                                     std::vector<uint8_t>& rightRgb,
                                     std::vector<uint8_t>& leftRgb)
{
  if (!G_LOADED) loadShelvesFromJson();
  const int rightLeds = computeStripLenRange(1, 24);
  const int leftLeds  = computeStripLenRange(25, 40);
  rightRgb.assign(std::max(0, rightLeds) * 3, 0);
  leftRgb.assign (std::max(0, leftLeds)  * 3, 0);

  const int useShelves = static_cast<int>(std::min<size_t>(G_SHELVES.size(), 40));
  for (int idx = 0; idx < useShelves; ++idx) {
    const int shelfNo = idx + 1;
    int x, y; shelfToXY(shelfNo, x, y);
    if (x < 0 || x >= targetFrame.cols || y < 0 || y >= targetFrame.rows) continue;
    const cv::Vec3b bgr = targetFrame.at<cv::Vec3b>(y, x);
    const uint8_t R = bgr[2], G = bgr[1], B = bgr[0];

    auto& shelvesVec = G_SHELVES[idx];
    const bool isRight = (shelfNo >= 1 && shelfNo <= 24);
    std::vector<uint8_t>& buf = isRight ? rightRgb : leftRgb;
    const int totalLeds = isRight ? rightLeds : leftLeds;

    for (const auto& seg : shelvesVec) {
      for (int i = 0; i < seg.count; ++i) {
        const int led = seg.startIndex + i;
        if (led < 0 || led >= totalLeds) continue;
        const int o = led * 3;
        buf[o + 0] = R; buf[o + 1] = G; buf[o + 2] = B;
      }
    }
  }
}

// Send a DDP stream (chunked) to host:port containing the whole LED buffer.
static bool sendDDP(const char* host, uint16_t port, const std::vector<uint8_t>& rgb)
{
    if (rgb.empty())
        return true; // nothing to send is "success"

    // --- Socket ---
    const int sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    // Prefer inet_pton over inet_addr (inet_addr is deprecated and returns -1 on error)
    if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        ::close(sockfd);
        return false;
    }

    // --- Chunking parameters ---
    constexpr size_t kUdpPayloadBudget = 1472; // IP(20)+UDP(8) deducted from MTU 1500
    constexpr size_t kHeaderLen        = DDP_HEADER_LEN; // 10
    static_assert(kHeaderLen == 10, "DDP header must be 10 bytes");

    // Max RGB bytes per packet; keep multiple of 3 (RGB)
    const size_t maxDataPerPacket = ((kUdpPayloadBudget - kHeaderLen) / 3) * 3; // e.g. 1461

    // Sequence can be static so it continues across frames (optional)
    static uint8_t seq = 0;

    size_t offsetBytes = 0;
    while (offsetBytes < rgb.size()) {
        const size_t remaining = rgb.size() - offsetBytes;
        const size_t chunkBytes = static_cast<uint16_t>(std::min(remaining, maxDataPerPacket)); // <= 65535

        // Build one DDP packet
        std::vector<uint8_t> packet;
        packet.resize(kHeaderLen + chunkBytes);

        // Byte 0: flags (version + push)
        packet[0] = DDP_FLAGS;

        // Byte 1: sequence (increments each packet; receiver can drop dupes/out-of-order)
        packet[1] = seq++;

        // Byte 2: data type (RGB)
        packet[2] = DDP_DATATYPE_RGB;

        // Byte 3: reserved
        packet[3] = 0;

        // Bytes 4..7: channel offset (big endian) — **in BYTES** for RGB streams
        const uint32_t chanOff = static_cast<uint32_t>(offsetBytes);
        packet[4] = static_cast<uint8_t>((chanOff >> 24) & 0xFF);
        packet[5] = static_cast<uint8_t>((chanOff >> 16) & 0xFF);
        packet[6] = static_cast<uint8_t>((chanOff >>  8) & 0xFF);
        packet[7] = static_cast<uint8_t>( chanOff        & 0xFF);

        // Bytes 8..9: data length (big endian) — number of data bytes in this packet
        const uint16_t dataLen = static_cast<uint16_t>(chunkBytes);
        packet[8] = static_cast<uint8_t>((dataLen >> 8) & 0xFF);
        packet[9] = static_cast<uint8_t>( dataLen       & 0xFF);

        // Payload
        std::memcpy(packet.data() + kHeaderLen, rgb.data() + offsetBytes, chunkBytes);

        // Send
        const ssize_t sent = ::sendto(
            sockfd,
            reinterpret_cast<const char*>(packet.data()),
            static_cast<int>(packet.size()),
            0,
            reinterpret_cast<sockaddr*>(&addr),
            sizeof(addr)
        );

        if (sent != static_cast<ssize_t>(packet.size())) {
            ::close(sockfd);
            return false;
        }

        offsetBytes += chunkBytes;
    }

    ::close(sockfd);
    return true;
}

// --- WiZ local UDP sender (host/port/IPs configured via g_net) ---
static void sendWiZColor(const char* ip, uint8_t r, uint8_t g, uint8_t b)
{
    // {"method":"setPilot","params":{"state":true,"r":R,"g":G,"b":B}}
    char payload[128];
    int n = snprintf(payload, sizeof(payload),
                     "{\"method\":\"setPilot\",\"params\":{\"state\":true,\"r\":%u,\"g\":%u,\"b\":%u}}",
                     (unsigned)r, (unsigned)g, (unsigned)b);
    if (n <= 0) return;

    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(g_net.wizPort));
    if (::inet_pton(AF_INET, ip, &addr.sin_addr) != 1) { ::close(sock); return; }

    (void)::sendto(sock, payload, n, 0, (sockaddr*)&addr, sizeof(addr));
    ::close(sock);
}



/*
// Call this from your calculateGameWallFrameFromFrame()
void AmbiPi::sendFrameToGameWall(const cv::Mat& resized6x4BGR)
{
  // Build LED buffer
  std::vector<uint8_t> rgb;
  buildLedBufferFromFrame(resized6x4BGR, rgb);

  // Send via DDP
  if (!sendDDP(rgb)) {
    // handle error (log, retry, etc.)
  }
}
*/
void AmbiPi::sendFrameToGameWall(const cv::Mat& resized6x4BGR)
{
    if (!_enableGameWallAmbilight) return;
    // Build raw LED buffers for this frame (per board)
    std::vector<uint8_t> rightCurr, leftCurr;
    buildLedBuffersFromFrame(resized6x4BGR, rightCurr, leftCurr);

    // Allocate/resize last frames once to match sizes
    if (lastFrame_.size() != rightCurr.size() + leftCurr.size()) {
        lastFrame_.assign(rightCurr.size() + leftCurr.size(), 0);
    }

    // Blend each buffer against its own slice in lastFrame_
    // Layout lastFrame_ = [ right | left ]
    auto blendInto = [this](std::vector<uint8_t>& curr, size_t base, const std::vector<uint8_t>& prevAll){
        for (size_t i = 0; i < curr.size(); ++i) {
            float blended = prevAll[base + i] * _alpha + curr[i] * (1.0f - _alpha);
            curr[i] = static_cast<uint8_t>(std::lround(blended));
        }
    };
    blendInto(rightCurr, 0, lastFrame_);
    blendInto(leftCurr,  rightCurr.size(), lastFrame_);

    // Update history
    lastFrame_.assign(rightCurr.begin(), rightCurr.end());
    lastFrame_.insert(lastFrame_.end(), leftCurr.begin(), leftCurr.end());

    // Send via DDP to each board
    if (!rightCurr.empty()) (void)sendDDP(g_net.ddpHostRight.c_str(), static_cast<uint16_t>(g_net.ddpPort), rightCurr);
    if (!leftCurr.empty())  (void)sendDDP(g_net.ddpHostLeft.c_str(),  static_cast<uint16_t>(g_net.ddpPort), leftCurr);
}


void AmbiPi::calculateGameWallFrameFromFrame(cv::Mat frame)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!_enableGameWallAmbilight) return;
    int w = frame.cols;
    int h = frame.rows;

    int interpolation = cv::INTER_AREA; // cv::INTER_LANCZOS4; // INTER_CUBIC

    cv::Mat targetFrame;
    cv::resize(frame, targetFrame, cv::Size(6, 4), 0, 0, interpolation);
    cv::flip(targetFrame, targetFrame, 1); // Mirror horizontally

    sendFrameToGameWall(targetFrame);
}

void AmbiPi::calculateKickerLightsFromFrame(cv::Mat frame)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    // Use the same toggle unless you want a dedicated one later
    if (!_enableGameWallAmbilight) return;

    if (frame.empty()) return;
    const int w = frame.cols;
    const int h = frame.rows;

    // Take centered square: full height if possible (landscape), otherwise clamp to width
    int side = h;
    if (side > w) side = w; // handle portrait input robustly
    const int x = (w - side) / 2;
    const int y = 0;

    cv::Mat square = frame(cv::Rect(x, y, side, side));

    // Downscale to 1x4 (BGR)
    cv::Mat small;
    cv::resize(square, small, cv::Size(1, 4), 0, 0, cv::INTER_AREA);

    // Extract four colors top->bottom and send to WiZ bulbs in given order
    // (order remains unchanged)
    for (int i = 0; i < 4; ++i) {
        const cv::Vec3b bgr = small.at<cv::Vec3b>(i, 0);
        uint8_t R = bgr[2];
        uint8_t G = bgr[1];
        uint8_t B = bgr[0];
        // optional gamma correction using LUT if configured
        if (!_lut.empty()) { R = _lut[R]; G = _lut[G]; B = _lut[B]; }
        const size_t wizIdx = static_cast<size_t>(3 - i);
        if (wizIdx < g_net.wizIps.size()) sendWiZColor(g_net.wizIps[wizIdx].c_str(), R, G, B);
    }

    // --- Extra two bulbs: sample left/right quarters (25% width each, full height) ---
    if (side >= 4) { // safety guard for very small frames
        const int qw = std::max(1, side / 4); // quarter width

        // Left quarter: x = 0 .. qw-1
        cv::Mat leftQ = square(cv::Rect(0, 0, qw, side));
        cv::Mat left1x1;
        cv::resize(leftQ, left1x1, cv::Size(1, 1), 0, 0, cv::INTER_AREA);
        {
            const cv::Vec3b bgr = left1x1.at<cv::Vec3b>(0, 0);
            uint8_t R = bgr[2], G = bgr[1], B = bgr[0];
            if (!_lut.empty()) { R = _lut[R]; G = _lut[G]; B = _lut[B]; }
            sendWiZColor(g_net.wizLeftQuarterIp.c_str(), R, G, B);
        }
/*
        // Right quarter: x = side-qw .. side-1
        cv::Mat rightQ = square(cv::Rect(side - qw, 0, qw, side));
        cv::Mat right1x1;
        cv::resize(rightQ, right1x1, cv::Size(1, 1), 0, 0, cv::INTER_AREA);
        {
            const cv::Vec3b bgr = right1x1.at<cv::Vec3b>(0, 0);
            uint8_t R = bgr[2], G = bgr[1], B = bgr[0];
            if (!_lut.empty()) { R = _lut[R]; G = _lut[G]; B = _lut[B]; }
            sendWiZColor(g_net.wizRightQuarterIp.c_str(), R, G, B);
        }
*/
    }
}

bool AmbiPi::sendFullFrame(cv::Mat frame)
{
	uint16_t size = 32*32*3;
	unsigned char buf[size+8];
	buf[0] = 'K';
	buf[1] = 'D';
	buf[2] = (uint8_t) ((size>>8) & 0xff);
	buf[3] = (uint8_t) ((size>>0) & 0xff);
	buf[4] = (uint8_t) g_net.displayPrio; // PRIO
	buf[5] = 25;   // TTL
	buf[6] = 0;    // TYPE
	buf[7] = 0;    // SECT
	memcpy(buf + 8, frame.ptr(0), 1024*3);

	sendKDPDatagram(buf, sizeof(buf));
	return true;
}

bool AmbiPi::sendKDPDatagram(const uint8_t* data, size_t size)
{
	sockaddr_in servaddr;
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd<0) {
		// qWarning("KickerDisplay::sendKDPDatagram() cannot open socket");
		return false;
	}

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr(g_net.displayServer.c_str());
	servaddr.sin_port = htons(static_cast<uint16_t>(g_net.displayPort));
	int len = sendto(fd, data, size, 0, (sockaddr*)&servaddr, sizeof(servaddr));
	if (len < 0) {
		perror("Cannot send message");
		::close(fd);
		// qWarning("Display::sendKDPDatagram() failed");
		return false;
	}
	::close(fd);
	// qDebug("Display::sendKDPDatagram('%s:%d') %d/%ld bytes", DISPLAY_SERVER, DISPLAY_PORT, len, size);
	return true;
}

void AmbiPi::calculateAmbilightFromFrame(cv::Mat frame, bool bgr)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    // Dimensions excluding the physical LED corners
    const int top   = LEDS_TOP    - 2;
    const int bot   = LEDS_BOTTOM - 2;
    const int left  = LEDS_LEFT   - 2;
    const int right = LEDS_RIGHT  - 2;

    // Sampling window sizes (integer, avoid reallocation thrash)
    const double factor = 4.0;
    const int dw = std::max(1, static_cast<int>(std::lround(factor * frame.cols / static_cast<double>(top ))));
    const int dh = std::max(1, static_cast<int>(std::lround(factor * frame.rows / static_cast<double>(left))));

    // Temporary 1D color strips (reuse buffers across calls)
    static cv::Mat colorsTopNew, colorsBottomNew, colorsLeftNew, colorsRightNew;
    colorsTopNew.create(1,   top,   CV_8UC3);
    colorsBottomNew.create(1, bot,  CV_8UC3);
    colorsLeftNew.create( left, 1,  CV_8UC3);
    colorsRightNew.create(right, 1, CV_8UC3);

    // Downsample edge regions into 1D strips (INTER_AREA is fast and good for downscaling)
    cv::resize(frame(cv::Rect(0,            0,          frame.cols, dh)), colorsTopNew,    cv::Size(top,  1), 0, 0, cv::INTER_AREA);
    cv::resize(frame(cv::Rect(0,  frame.rows - dh,      frame.cols, dh)), colorsBottomNew, cv::Size(bot,  1), 0, 0, cv::INTER_AREA);
    cv::resize(frame(cv::Rect(0,            0,          dw, frame.rows)), colorsLeftNew,  cv::Size(1, left),  0, 0, cv::INTER_AREA);
    cv::resize(frame(cv::Rect(frame.cols - dw, 0,       dw, frame.rows)), colorsRightNew, cv::Size(1, right), 0, 0, cv::INTER_AREA);

    // Exponential smoothing of strips into persistent members
    cv::addWeighted(_colorsT, _alpha, colorsTopNew,    1.0 - _alpha, 0.0, _colorsT);
    cv::addWeighted(_colorsB, _alpha, colorsBottomNew, 1.0 - _alpha, 0.0, _colorsB);
    cv::addWeighted(_colorsL, _alpha, colorsLeftNew,   1.0 - _alpha, 0.0, _colorsL);
    cv::addWeighted(_colorsR, _alpha, colorsRightNew,  1.0 - _alpha, 0.0, _colorsR);

    // Color channel indices (OpenCV holds BGR)
    const int r = bgr ? 0 : 2;
    const int g = 1;
    const int b = bgr ? 2 : 0;

    // Use row pointers for fast access (avoid at<> overhead)
    const cv::Vec3b* pL = _colorsL.ptr<cv::Vec3b>();
    const cv::Vec3b* pB = _colorsB.ptr<cv::Vec3b>();
    const cv::Vec3b* pR = _colorsR.ptr<cv::Vec3b>();
    const cv::Vec3b* pT = _colorsT.ptr<cv::Vec3b>();

    // --- Left interior (shift down by one; corner handled separately) ---
    for (int i = 1; i < LEDS_LEFT; ++i) {
        const cv::Vec3b& c = pL[i - 1]; // pL has length `left`, maps to LED indices 1..LEDS_LEFT-1
        setColorLeft(i - 1, c[r], c[g], c[b]);
    }

    // --- Bottom interior (exclude corners) ---
    for (int i = 0; i < bot; ++i) {
        const cv::Vec3b& c = pB[i];
        setColorBottom(i + 1, c[r], c[g], c[b]);
    }

    // --- Right interior (exclude corners; reverse order) ---
    for (int i = right - 1; i >= 0; --i) {
        const cv::Vec3b& c = pR[i];
        setColorRight(i + 1, c[r], c[g], c[b]);
    }

    // --- Top interior (exclude corners; reverse order) ---
    for (int i = top - 1; i >= 0; --i) {
        const cv::Vec3b& c = pT[i];
        setColorTop(i + 1, c[r], c[g], c[b]);
    }

    // --- Corners: average of adjacent strip ends ---
    const cv::Vec3b TL = pT[0]; // (pT[0]       + pL[0])        * 0.5;
    const cv::Vec3b TR = pT[top-1]; // (pT[top-1]   + pR[0])        * 0.5;
    const cv::Vec3b BL = pB[0]; // (pB[0]       + pL[left-3])   * 0.5;
    const cv::Vec3b BR = pB[bot-1]; // (pB[bot-1]   + pR[right-1])  * 0.5;

    setColorTop(0,              TL[r], TL[g], TL[b]);
    setColorLeft(0,             TL[r], TL[g], TL[b]);

    setColorTop(LEDS_TOP-1,     TR[r], TR[g], TR[b]);
    setColorRight(0,            TR[r], TR[g], TR[b]);

    setColorBottom(0,           BL[r], BL[g], BL[b]);
    setColorLeft(LEDS_LEFT-1,   BL[r], BL[g], BL[b]);

    setColorBottom(LEDS_BOTTOM-1, BR[r], BR[g], BR[b]);
    setColorRight(LEDS_RIGHT-1,   BR[r], BR[g], BR[b]);

    // Keep your explicit fix: second/third from bottom-left forced to corner color
    setColorLeft(LEDS_LEFT-2, BL[r], BL[g], BL[b]);
    setColorLeft(LEDS_LEFT-3, BL[r], BL[g], BL[b]);
}

void AmbiPi::setLastFrame(cv::Mat frame)
{
	_mutex.lock();
	_lastFrame = frame.clone();
	_mutex.unlock();
}

cv::Mat AmbiPi::lastFrame() const
{
	cv::Mat frame;
	_mutex.lock();
	frame = _lastFrame.clone();
	_mutex.unlock();
	return frame;
}

bool AmbiPi::getEnableDisplayVideo() const
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	return _enableDisplayVideo;
}

void AmbiPi::setEnableDisplayVideo(bool enableDisplayVideo)
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	_enableDisplayVideo = enableDisplayVideo;
}

bool AmbiPi::getEnableGamingTable() const
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	return _enableGamingTable;
}

void AmbiPi::setEnableGamingTable(bool enableGamingTable)
{
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	_enableGamingTable = enableGamingTable;
}

bool AmbiPi::getEnableGameWallAmbilight() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _enableGameWallAmbilight;
}

void AmbiPi::setEnableGameWallAmbilight(bool enable)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _enableGameWallAmbilight = enable;
}

bool AmbiPi::getSwapRB() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _swapRB;
}

void AmbiPi::setSwapRB(bool swap)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _swapRB = swap;
    saveSettings();
}

// --- Persistent runtime settings (UI-toggled), kept out of the config.json
// conffile so upgrades/restarts don't clobber them. Stored under the systemd
// WorkingDirectory /var/lib/ambipi.
static const char* SETTINGS_PATH = "/var/lib/ambipi/settings.json";

void AmbiPi::loadSettings()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    std::ifstream ifs(SETTINGS_PATH);
    if (!ifs) return;
    try {
        nlohmann::json j; ifs >> j;
        _swapRB = j.value("swap_rb", _swapRB);
        std::cerr << "[INFO] Loaded settings from " << SETTINGS_PATH
                  << " (swap_rb=" << (_swapRB ? "true" : "false") << ")\n";
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] settings.json parse error: " << e.what() << "\n";
    }
}

void AmbiPi::saveSettings() const
{
    nlohmann::json j;
    j["swap_rb"] = _swapRB;
    std::ofstream ofs(SETTINGS_PATH);
    if (!ofs) { std::cerr << "[ERROR] Cannot write " << SETTINGS_PATH << "\n"; return; }
    ofs << j.dump(2) << "\n";
}
