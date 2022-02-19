#include "ambipi.h"
#include "unistd.h"
#include <array>
#include <math.h>

#include <arpa/inet.h>
#include <unistd.h>
#include "rpi_ws281x/ws2811.h"

// 30 leds/meter

#define LEDS_TOP	(60)
#define LEDS_BOTTOM	(60)
#define LEDS_LEFT	(34)
#define LEDS_RIGHT	(34)

//#define LEDS_TOP	(16)
//#define LEDS_BOTTOM	(16)
//#define LEDS_LEFT	(9)
//#define LEDS_RIGHT	(9)

#define TARGET_FREQ             WS2811_TARGET_FREQ
#define GPIO_PIN1               12
#define GPIO_PIN2               13
#define WS2811_DMA              10
#define MAX_BRIGHTNESS		255

#ifdef DEVEL
#define DISPLAY_SERVER "127.0.0.1"
#else
#define DISPLAY_SERVER "192.168.178.104"
#endif
#define DISPLAY_PORT   14000

AmbiPi::AmbiPi() : _mode(Off), _alpha(0.85), _gamma(0), _enableCropping(false)
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
	setLastFrame(cv::Mat(480, 720, CV_8UC3, cv::Scalar(b,g,r)));
	_cropRect = cv::Rect(0, 0, 720, 480);
}

bool AmbiPi::init(double gamma)
{
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
	_enableCropping = cropping;
}

bool AmbiPi::croppingEnabled() const
{
	return _enableCropping;
}

void AmbiPi::setBrightness(uint8_t bri)
{
	_ws2811->channel[0].brightness	= bri;
	_ws2811->channel[1].brightness	= bri;
}

uint8_t AmbiPi::brightness() const
{
	return _ws2811->channel[0].brightness;
}

void AmbiPi::setAlpha(double alpha)
{
	_alpha = alpha;
}

double AmbiPi::alpha() const
{
	return _alpha;
}


void AmbiPi::setGamma(double gamma)
{
	_gamma = gamma;
	if (gamma != 0) {
		ws2811_set_custom_gamma_factor(_ws2811, gamma);
	}
}

double AmbiPi::gamma() const
{
	return _gamma;
}

void AmbiPi::clear()
{
	setColor(0,0,0);
}

void AmbiPi::setColor(uint8_t r, uint8_t g, uint8_t  b)
{
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

int AmbiPi::rainbow(int cnt)
{
	int c = ledCount();
	uint8_t r,g,b;
	for (int i = 0; i<c; i++) {
		uint8_t pos = (255*i / (c-1) + cnt);
		getRainbowColor(pos, r, g, b);
		setColor(i, r, g, b);
	}
	return 25;
}

void AmbiPi::render()
{
#ifdef _GUI_
	return;
#endif
	// ws2811_wait(_ws2811);
	ws2811_render(_ws2811);
}

cv::Mat AmbiPi::getDebugFrame(cv::Mat frame) const
{
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
				frame.at<std::array<uint8_t,3>>(y,x)[0] = 0*rand() % 256;
				frame.at<std::array<uint8_t,3>>(y,x)[1] = 0*rand() % 256;
				frame.at<std::array<uint8_t,3>>(y,x)[2] = 0xff; // rand() % 256;
			} else {
				frame.at<std::array<uint8_t,3>>(y,x)[0] = 0xff;
				frame.at<std::array<uint8_t,3>>(y,x)[1] = 0;
				frame.at<std::array<uint8_t,3>>(y,x)[2] = 0;
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
	int w = frame.cols;
	int h = frame.rows;

	int interpolation = cv::INTER_CUBIC; // INTER_CUBIC

	cv::Mat squareFrame;
	cv::resize(frame(cv::Rect((w-h)/2,0, h, h)), squareFrame, cv::Size(32, 32), 0, 0, interpolation);

	cv::Mat rgbFrame;
	cv::cvtColor(squareFrame, rgbFrame, cv::COLOR_RGB2BGR);
	sendFullFrame(rgbFrame);
}



bool AmbiPi::sendFullFrame(cv::Mat frame)
{
	uint16_t size = 32*32*3;
	unsigned char buf[size+8];
	buf[0] = 'K';
	buf[1] = 'D';
	buf[2] = (uint8_t) ((size>>8) & 0xff);
	buf[3] = (uint8_t) ((size>>0) & 0xff);
	buf[4] = 0xa0; // PRIO
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
	servaddr.sin_addr.s_addr = inet_addr(DISPLAY_SERVER);
	servaddr.sin_port = htons(DISPLAY_PORT);
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
	// setColor(0,255,0);
	int top   = LEDS_TOP     - 2;
	int bot   = LEDS_BOTTOM  - 2;
	int left  = LEDS_LEFT    - 2;
	int right = LEDS_RIGHT   - 2;

	double factor = 4.0;
	double dw = factor * frame.cols / (double) top;
	double dh = factor * frame.rows / (double) left;


	int interpolation = cv::INTER_LINEAR; // INTER_CUBIC
	cv::Mat colorsTop, colorsBottom, colorsLeft, colorsRight;
	cv::resize(frame(cv::Rect(0,0,frame.cols, dh)), colorsTop, cv::Size(top, 1), 0, 0, interpolation);
	cv::resize(frame(cv::Rect(0, frame.rows-dh, frame.cols, dh)), colorsBottom, cv::Size(bot, 1), 0, 0, interpolation);
	cv::resize(frame(cv::Rect(0,0, dw, frame.rows)), colorsLeft, cv::Size(1, left), 0, 0, interpolation);
	cv::resize(frame(cv::Rect(frame.cols-dw, 0, dw, frame.rows)), colorsRight, cv::Size(1, right), 0, 0, interpolation);
	cv::addWeighted(_colorsT, _alpha, colorsTop,    1.0 - _alpha, 0.0, _colorsT);
	cv::addWeighted(_colorsB, _alpha, colorsBottom, 1.0 - _alpha, 0.0, _colorsB);
	cv::addWeighted(_colorsL, _alpha, colorsLeft,   1.0 - _alpha, 0.0, _colorsL);
	cv::addWeighted(_colorsR, _alpha, colorsRight,  1.0 - _alpha, 0.0, _colorsR);
	cv::Vec3b c;
	int r,g,b;
	if (bgr) {
		r = 0;
		g = 1;
		b = 2;
	} else {
		r = 2;
		g = 1;
		b = 0;
	}
	for (int i=0; i<LEDS_LEFT-2; i++) {
		c = _colorsL.at<cv::Vec3b>(cv::Point(0, i));
		setColorLeft(i+1, c[r], c[g], c[b]);
	}
	setColorLeft(LEDS_LEFT-1, c[r], c[g], c[b]);
	setColorBottom(0, c[r], c[g], c[b]);

	for (int i=0; i<LEDS_BOTTOM-2; i++) {
		c = _colorsB.at<cv::Vec3b>(cv::Point(i, 0));
		setColorBottom(i+1, c[r], c[g], c[b]);
	}
	setColorBottom(LEDS_BOTTOM-1, c[r], c[g], c[b]);
	setColorRight(LEDS_RIGHT-1, c[r], c[g], c[b]);

	for (int i=LEDS_RIGHT-3; i>=0; i--) {
		c = _colorsR.at<cv::Vec3b>(cv::Point(0, i));
		setColorRight(i+1, c[r], c[g], c[b]);
	}
	setColorRight(0, c[r], c[g], c[b]);
	setColorTop(LEDS_TOP-1, c[r], c[g], c[b]);

	for (int i=LEDS_TOP-3; i>=0; i--) {
		c = _colorsT.at<cv::Vec3b>(cv::Point(i, 0));
		setColorTop(i+1, c[r], c[g], c[b]);
	}
	setColorTop( 0, c[r], c[g], c[b]);
	setColorLeft(0, c[r], c[g], c[b]);
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
	return _enableDisplayVideo;
}

void AmbiPi::setEnableDisplayVideo(bool enableDisplayVideo)
{
	_enableDisplayVideo = enableDisplayVideo;
}
