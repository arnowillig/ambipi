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

	cv::resize(frame, frame, cv::Size(), 0.5, 0.5, cv::INTER_LINEAR);

	const int border = 80;
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

	cv::blur(out, out, cv::Size(fw*(dwT+dwB)/2, fh*(dhL+dhR)/2));
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
		cv::putText(out, std::to_string(x), cv::Point(oxx+(x+0.125)*dwT,oyy+(LEDS_LEFT+1.5)*dhL), cv::FONT_HERSHEY_COMPLEX_SMALL, 0.5, cv::Scalar(128,128,128), 1, cv::LINE_AA);
	}
	for (int y=0; y < LEDS_LEFT; y++) {
		uint32_t col = _ws2811->channel[0].leds[LEDS_LEFT-y-1];
		cv::Vec3b colorL = cv::Vec3b((col>>0) & 0xff, (col>>8) & 0xff , (col>>16) & 0xff);
		cv::rectangle(out, cv::Rect(oxx-dwT, oyy + (y)*dhL, dwT, dhL), colorL, -1);
		cv::rectangle(out, cv::Rect(oxx-dwT, oyy + (y)*dhL, dwT, dhL), cv::Scalar(128,128,128), 1);
		cv::putText(out, std::to_string(LEDS_LEFT-y-1), cv::Point(oxx-2*dwT, oyy + (y+0.75)*dhL), cv::FONT_HERSHEY_COMPLEX_SMALL, 0.5, cv::Scalar(128,128,128), 1, cv::LINE_AA);
	}
	for (int y=0; y < LEDS_RIGHT; y++) {
		uint32_t col = _ws2811->channel[1].leds[LEDS_BOTTOM+LEDS_RIGHT-y-1];
		cv::Vec3b colorL = cv::Vec3b((col>>0) & 0xff, (col>>8) & 0xff , (col>>16) & 0xff);
		cv::rectangle(out, cv::Rect(oxx+(LEDS_TOP*dwT), oyy + (y)*dhL, dwT, dhL), colorL, -1);
		cv::rectangle(out, cv::Rect(oxx+(LEDS_TOP*dwT), oyy + (y)*dhL, dwT, dhL), cv::Scalar(128,128,128), 1);
		cv::putText(out, std::to_string(LEDS_BOTTOM+LEDS_RIGHT-y-1), cv::Point(oxx+((LEDS_TOP+1.5)*dwT), oyy + (y+0.75)*dhL), cv::FONT_HERSHEY_COMPLEX_SMALL, 0.5, cv::Scalar(128,128,128), 1, cv::LINE_AA);
	}

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

void AmbiPi::calculateAmbilightFromFrame(cv::Mat frame, double alpha)
{
	// setColor(0,255,0);
	int top   = LEDS_TOP     - 2;
	int bot   = LEDS_BOTTOM  - 2;
	int left  = LEDS_LEFT    - 2;
	int right = LEDS_RIGHT   - 2;

	double dw = frame.cols / (double) top;
	double dh = frame.rows / (double) left;

	 
	int interpolation = cv::INTER_LINEAR; // INTER_CUBIC
	cv::Mat colorsTop, colorsBottom, colorsLeft, colorsRight;
	cv::resize(frame(cv::Rect(0,0,frame.cols, dh)), colorsTop, cv::Size(top, 1), 0, 0, interpolation);
	cv::resize(frame(cv::Rect(0, frame.rows-dh, frame.cols, dh)), colorsBottom, cv::Size(bot, 1), 0, 0, interpolation);
	cv::resize(frame(cv::Rect(0,0, dw, frame.rows)), colorsLeft, cv::Size(1, left), 0, 0, interpolation);
	cv::resize(frame(cv::Rect(frame.cols-dw, 0, dw, frame.rows)), colorsRight, cv::Size(1, right), 0, 0, interpolation);
	cv::addWeighted(_colorsT, alpha, colorsTop,    1.0 - alpha, 0.0, _colorsT);
	cv::addWeighted(_colorsB, alpha, colorsBottom, 1.0 - alpha, 0.0, _colorsB);
	cv::addWeighted(_colorsL, alpha, colorsLeft,   1.0 - alpha, 0.0, _colorsL);
	cv::addWeighted(_colorsR, alpha, colorsRight,  1.0 - alpha, 0.0, _colorsR);
	cv::Vec3b c;
	for (int i=0; i<LEDS_LEFT-2; i++) {
		c = _colorsL.at<cv::Vec3b>(cv::Point(0, i));
		setColorLeft(i+1, c[2], c[1], c[0]);
	}	
	setColorLeft(LEDS_LEFT-1, c[2], c[1], c[0]);
	setColorBottom(0, c[2], c[1], c[0]);

	for (int i=0; i<LEDS_BOTTOM-2; i++) {
		c = _colorsB.at<cv::Vec3b>(cv::Point(i, 0));
		setColorBottom(i+1, c[2], c[1], c[0]);
	}
	setColorBottom(LEDS_BOTTOM-1, c[2], c[1], c[0]);
	setColorRight(LEDS_RIGHT-1, c[2], c[1], c[0]);

	for (int i=LEDS_RIGHT-3; i>=0; i--) {
		c = _colorsR.at<cv::Vec3b>(cv::Point(0, i));
		setColorRight(i+1, c[2], c[1], c[0]);
	}
	setColorRight(0, c[2], c[1], c[0]);
	setColorTop(LEDS_TOP-1, c[2], c[1], c[0]);

	for (int i=LEDS_TOP-3; i>=0; i--) {
		c = _colorsT.at<cv::Vec3b>(cv::Point(i, 0));
		setColorTop(i+1, c[2], c[1], c[0]);
	}
	setColorTop( 0, c[2], c[1], c[0]);
	setColorLeft(0, c[2], c[1], c[0]);




}
