#include "ambipi.h"
#include "unistd.h"
#include <array>

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
#define GPIO_PIN1               18
#define GPIO_PIN2               13
#define WS2811_DMA              10
#define MAX_BRIGHTNESS		255

AmbiPi::AmbiPi() : _mode(Off)
{
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
	uint8_t a = 2;
	_colorsL = cv::Mat(LEDS_LEFT - a, 1,   CV_8UC3, cv::Scalar(b, g, r));
	_colorsT = cv::Mat(1, LEDS_TOP - a,    CV_8UC3, cv::Scalar(b, g, r));
	_colorsB = cv::Mat(1, LEDS_BOTTOM - a, CV_8UC3, cv::Scalar(b, g, r));
	_colorsR = cv::Mat(LEDS_RIGHT - a, 1,  CV_8UC3, cv::Scalar(b, g, r));
}

AmbiPi::~AmbiPi()
{
	if (_ws2811) {
		ws2811_fini(_ws2811);
		free(_ws2811);
	}
}

void AmbiPi::setBrightness(uint8_t bri)
{
	_ws2811->channel[0].brightness	= bri;
	_ws2811->channel[1].brightness	= bri;

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

void AmbiPi::drawTestPattern(int i, int bri)
{
	int r = 255;
	int g = 255;
	int b = 255;
	int cnt = (i / 2)  % ledCount();
        setColorLeft  (bri,  0,    0);
        setColorBottom(bri,   0, bri);
        setColorTop   (  0, bri, bri);
        setColorRight (  0, bri,   0);
        
       	_ws2811->channel[0].leds[cnt % (LEDS_LEFT+LEDS_TOP)]     = ((r & 0x0ff) << 16) | ((g & 0x0ff) << 8) | (b & 0x0ff);
       	_ws2811->channel[1].leds[(LEDS_BOTTOM+LEDS_RIGHT) -1 - (cnt % (LEDS_BOTTOM+LEDS_RIGHT))] = ((r & 0x0ff) << 16) | ((g & 0x0ff) << 8) | (b & 0x0ff);
}

int AmbiPi::ledCount() const
{
	return LEDS_LEFT + LEDS_TOP + LEDS_RIGHT + LEDS_BOTTOM;
}

void AmbiPi::rainbow(int cnt)
{
	int c = ledCount() / 2;
	uint8_t r,g,b;
	for (int i = 0; i<c; i++) {
		uint8_t pos = (255*i / (c-1) + cnt) % 256;
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
		_ws2811->channel[0].leds[i] = ((r & 0x0ff) << 16) | ((g & 0x0ff) << 8) | (b & 0x0ff);
		_ws2811->channel[1].leds[i] = ((r & 0x0ff) << 16) | ((g & 0x0ff) << 8) | (b & 0x0ff);
	}
	// ws2811_render(_ws2811);
}

void AmbiPi::render()
{
#ifdef _DEVEL_
	return;
#endif
	ws2811_wait(_ws2811);
	/*
	if (_ws2811->render_wait_time>0) {
		//fprintf(stderr, "Wait for render: %d msec\n", _ws2811->render_wait_time);
		usleep(_ws2811->render_wait_time);
	}
	*/
	ws2811_render(_ws2811);

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
	
#ifdef _DEVEL_
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

	if (gamma != 0) {
		ws2811_set_custom_gamma_factor(_ws2811, gamma);
	}
	(void) gamma;
	return true;
}

#ifdef _DEVEL_
void AmbiPi::drawGUI(cv::Mat frame)
{
	if (frame.empty()) {
		return;
	}
	calculateAmbilightFromFrame(frame, 0.95);

	const int border = 80;
	cv::Mat out(frame.rows+2*border, frame.cols+2*border, CV_8UC3, cv::Scalar(0, 0, 0));

	int ox = (out.cols - frame.cols) / 2;
	int oy = (out.rows - frame.rows) / 2;

	double fw = out.cols / (double) frame.cols;
	double fh = out.rows / (double) frame.rows;

	double dwT = frame.cols / (double) LEDS_TOP;
	double dwB = frame.cols / (double) LEDS_BOTTOM;
	double dhL = frame.rows / (double) LEDS_LEFT;
	double dhR = frame.rows / (double) LEDS_RIGHT;

	cv::Point pts[4];
	for (int x=0; x < LEDS_TOP; x++) {
		cv::Vec3b colorT = _colorsT.at<cv::Vec3b>(cv::Point(x, 0));
		pts[1] = cv::Point((x+0)*fw*dwT, 0);
		pts[2] = cv::Point((x+1)*fw*dwT, 0);
		pts[3] = cv::Point(ox+(x+1)*dwT, oy);
		pts[0] = cv::Point(ox+(x+0)*dwT, oy);
		cv::fillConvexPoly(out, pts, 4, colorT);
	}

	for (int x=0; x < LEDS_BOTTOM; x++) {
		cv::Vec3b colorB = _colorsB.at<cv::Vec3b>(cv::Point(x,  0));
		pts[1] = cv::Point((x+0)*fw*dwB, out.rows-1);
		pts[2] = cv::Point((x+1)*fw*dwB, out.rows-1);
		pts[3] = cv::Point(ox+(x+1)*dwB, oy+frame.rows);
		pts[0] = cv::Point(ox+(x+0)*dwB, oy+frame.rows);
		cv::fillConvexPoly(out, pts, 4, colorB);
	}

	for (int y=0; y < LEDS_LEFT; y++) {
		cv::Vec3b colorL = _colorsL.at<cv::Vec3b>(cv::Point(0, y));
		pts[1] = cv::Point(0, (y+0)*fh*dhL);
		pts[2] = cv::Point(0, (y+1)*fh*dhL);
		pts[3] = cv::Point(ox, oy+(y+1)*dhL);
		pts[0] = cv::Point(ox, oy+(y+0)*dhL);
		cv::fillConvexPoly(out, pts, 4, colorL);
	}

	for (int y=0; y < LEDS_RIGHT; y++) {
		cv::Vec3b colorR = _colorsR.at<cv::Vec3b>(cv::Point(0, y));
		pts[1] = cv::Point(out.cols-1, (y+0)*fh*dhR);
		pts[2] = cv::Point(out.cols-1, (y+1)*fh*dhR);
		pts[3] = cv::Point(ox+frame.cols, oy+(y+1)*dhR);
		pts[0] = cv::Point(ox+frame.cols, oy+(y+0)*dhR);
		cv::fillConvexPoly(out, pts, 4, colorR);
	}

	cv::blur(out, out, cv::Size(fw*(dwT+dwB)/2, fh*(dhL+dhR)/2));
	frame.copyTo(out(cv::Rect(ox, oy, frame.cols, frame.rows)));

	cv::imshow("AmbiPi", out);
}
#endif

cv::Mat AmbiPi::createTestImage(int w, int h)
{
	uint8_t r = 0xff;
	uint8_t g = 0xff;
	uint8_t b = 0xff;
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
void AmbiPi::calculateAmbilightFromFrame(cv::Mat frame, double alpha)
{
	setColor(0,255,0);
	double dw = frame.cols / (double) LEDS_TOP;
	double dh = frame.rows / (double) LEDS_LEFT;

	int top   = LEDS_TOP     - 2;
	int bot   = LEDS_BOTTOM  - 2;
	int left  = LEDS_LEFT    - 2;
	int right = LEDS_RIGHT   - 2;
	 
	int interpolation = cv::INTER_LINEAR; // INTER_CUBIC
	cv::Mat colorsTop, colorsBottom, colorsLeft, colorsRight;
	cv::resize(frame(cv::Rect(0,0,frame.cols, dh)), colorsTop, cv::Size(top, 1), 0, 0, interpolation);
	cv::resize(frame(cv::Rect(0, frame.rows-dh, frame.cols, dh)), colorsBottom, cv::Size(bot, 1), 0, 0, interpolation); // INTER_CUBIC
	cv::resize(frame(cv::Rect(0,0, dw, frame.rows)), colorsLeft, cv::Size(1, left), 0, 0, interpolation);
	cv::resize(frame(cv::Rect(frame.cols-dw, 0, dw, frame.rows)), colorsRight, cv::Size(1, right), 0, 0, interpolation); // INTER_CUBIC
	cv::addWeighted(_colorsT, alpha, colorsTop,    1.0 - alpha, 0.0, _colorsT);
	cv::addWeighted(_colorsB, alpha, colorsBottom, 1.0 - alpha, 0.0, _colorsB);
	cv::addWeighted(_colorsL, alpha, colorsLeft,   1.0 - alpha, 0.0, _colorsL);
	cv::addWeighted(_colorsR, alpha, colorsRight,  1.0 - alpha, 0.0, _colorsR);
	cv::Vec3b c;
	for (int i=0; i<LEDS_LEFT-1; i++) {
		c = _colorsL.at<cv::Vec3b>(cv::Point(0, i));
		_ws2811->channel[0].leds[-1+LEDS_LEFT-1-i] = ((c[2] & 0xff) << 16) | ((c[1] & 0xff) << 8) | (c[0] & 0xff);
	}
	c = _colorsL.at<cv::Vec3b>(cv::Point(0, 0));
	_ws2811->channel[0].leds[LEDS_LEFT-1] = ((c[2] & 0xff) << 16) | ((c[1] & 0xff) << 8) | (c[0] & 0xff); // LEFT
	_ws2811->channel[0].leds[LEDS_LEFT-0] = ((c[2] & 0xff) << 16) | ((c[1] & 0xff) << 8) | (c[0] & 0xff); // TOP
	
	for (int i=0; i<LEDS_TOP-1; i++) {
		c = _colorsT.at<cv::Vec3b>(cv::Point(i, 0));
		_ws2811->channel[0].leds[1+i+LEDS_LEFT] = ((c[2] & 0xff) << 16) | ((c[1] & 0xff) << 8) | (c[0] & 0xff);
	}
	c = _colorsT.at<cv::Vec3b>(cv::Point(LEDS_TOP-3, 0));
	_ws2811->channel[0].leds[LEDS_TOP+LEDS_LEFT-1] = ((c[2] & 0xff) << 16) | ((c[1] & 0xff) << 8) | (c[0] & 0xff);
	
	for (int i=0; i<LEDS_BOTTOM-1; i++) {
		c = _colorsB.at<cv::Vec3b>(cv::Point(i, 0));
		_ws2811->channel[1].leds[1+i] = ((c[2] & 0xff) << 16) | ((c[1] & 0xff) << 8) | (c[0] & 0xff);
	}
	c = _colorsB.at<cv::Vec3b>(cv::Point(0, 0));
	_ws2811->channel[1].leds[0] = ((c[2] & 0xff) << 16) | ((c[1] & 0xff) << 8) | (c[0] & 0xff);
	
	for (int i=0; i<LEDS_RIGHT-1; i++) {
		c = _colorsR.at<cv::Vec3b>(cv::Point(0, i));
		_ws2811->channel[1].leds[-2+LEDS_BOTTOM+LEDS_RIGHT-1-i] = ((c[2] & 0xff) << 16) | ((c[1] & 0xff) << 8) | (c[0] & 0xff);
	}
	c = _colorsR.at<cv::Vec3b>(cv::Point(0, 0));
	_ws2811->channel[1].leds[LEDS_BOTTOM+LEDS_RIGHT-2] = ((c[2] & 0xff) << 16) | ((c[1] & 0xff) << 8) | (c[0] & 0xff);
	_ws2811->channel[1].leds[LEDS_BOTTOM+LEDS_RIGHT-1] = ((c[2] & 0xff) << 16) | ((c[1] & 0xff) << 8) | (c[0] & 0xff);

	c = _colorsB.at<cv::Vec3b>(cv::Point(LEDS_BOTTOM-3, 0));
	_ws2811->channel[1].leds[LEDS_BOTTOM-1] = ((c[2] & 0xff) << 16) | ((c[1] & 0xff) << 8) | (c[0] & 0xff);
}
