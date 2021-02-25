#include "ambipi.h"
#include "unistd.h"

// 30 leds/meter

#define LEDS_TOP	(60)
#define LEDS_BOTTOM	(60)
#define LEDS_LEFT	(35)
#define LEDS_RIGHT	(35)

//#define LEDS_TOP	(16)
//#define LEDS_BOTTOM	(16)
//#define LEDS_LEFT	(9)
//#define LEDS_RIGHT	(9)

#define TARGET_FREQ             WS2811_TARGET_FREQ
#define GPIO_PIN1               18
#define GPIO_PIN2               13
#define DMA                     10
#define MAX_BRIGHTNESS		0x80

AmbiPi::AmbiPi()
{
	clear();
}

AmbiPi::~AmbiPi()
{
	if (_ws2811) {
		ws2811_fini(_ws2811);
		free(_ws2811);
	}
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
	_colorsL = cv::Mat(LEDS_LEFT, 1,   CV_8UC3, cv::Scalar(b, g, r));
}
void AmbiPi::setColorTop(uint8_t r, uint8_t g, uint8_t  b)
{
	_colorsT = cv::Mat(1, LEDS_TOP,    CV_8UC3, cv::Scalar(b, g, r));
}
void AmbiPi::setColorBottom(uint8_t r, uint8_t g, uint8_t  b)
{
	_colorsB = cv::Mat(1, LEDS_BOTTOM, CV_8UC3, cv::Scalar(b, g, r));
}
void AmbiPi::setColorRight(uint8_t r, uint8_t g, uint8_t  b)
{
	_colorsR = cv::Mat(LEDS_RIGHT, 1,  CV_8UC3, cv::Scalar(b, g, r));
}

#include <array>

void AmbiPi::drawTestPattern(int i, int bri)
{
	int cnt = (i / 2)  % ledCount();
        setColorLeft  (bri,  0,    0);
        setColorTop   (  0, bri, bri);
        setColorBottom(  0, bri,   0);
        setColorRight (bri,   0, bri);
        
        if (cnt < LEDS_LEFT) {
	        _colorsL.at<std::array<uint8_t,3>>(cnt % LEDS_LEFT, 0)[0] = 0; 
	        _colorsL.at<std::array<uint8_t,3>>(cnt % LEDS_LEFT, 0)[1] = 0; 
	        _colorsL.at<std::array<uint8_t,3>>(cnt % LEDS_LEFT, 0)[2] = 40; 
        } else if (cnt < LEDS_LEFT + LEDS_TOP) {
	        cnt -= LEDS_LEFT;
        	_colorsT.at<std::array<uint8_t,3>>(0,cnt % LEDS_TOP)[0] = 0; 
        	_colorsT.at<std::array<uint8_t,3>>(0,cnt % LEDS_TOP)[1] = 0; 
        	_colorsT.at<std::array<uint8_t,3>>(0,cnt % LEDS_TOP)[2] = 40; 
        } else if (cnt<LEDS_LEFT + LEDS_TOP + LEDS_RIGHT) {
        	cnt -= LEDS_LEFT + LEDS_TOP;
        	cnt = LEDS_RIGHT -  1 - cnt;
	        _colorsR.at<std::array<uint8_t,3>>(cnt % LEDS_RIGHT,0)[0] = 0; 
	      	_colorsR.at<std::array<uint8_t,3>>(cnt % LEDS_RIGHT,0)[1] = 0; 
	      	_colorsR.at<std::array<uint8_t,3>>(cnt % LEDS_RIGHT,0)[2] = 40; 
        } else  {
        	cnt -= LEDS_LEFT + LEDS_TOP + LEDS_RIGHT;
        	cnt = LEDS_BOTTOM - 1 - cnt;
	        _colorsB.at<std::array<uint8_t,3>>(0,cnt % LEDS_BOTTOM)[0] = 0; 
	        _colorsB.at<std::array<uint8_t,3>>(0,cnt % LEDS_BOTTOM)[1] = 0; 
	        _colorsB.at<std::array<uint8_t,3>>(0,cnt % LEDS_BOTTOM)[2] = 40; 
        }



        render();
        
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
	ws2811_render(_ws2811);
}

void AmbiPi::render()
{
	if (_ws2811->render_wait_time>0) {
		fprintf(stderr, "Wait for render: %d\n", _ws2811->render_wait_time);
		usleep(_ws2811->render_wait_time);
	}

	for (int i=0; i<LEDS_LEFT; i++) {
		cv::Vec3b c = _colorsL.at<cv::Vec3b>(cv::Point(0, i));
		_ws2811->channel[0].leds[i] = ((c[2] & 0x0ff) << 16) | ((c[1] & 0x0ff) << 8) | (c[0] & 0x0ff);
	}
	for (int i=0; i<LEDS_TOP; i++) {
		cv::Vec3b c = _colorsT.at<cv::Vec3b>(cv::Point(i, 0));
		_ws2811->channel[0].leds[i+LEDS_LEFT] = ((c[2] & 0x0ff) << 16) | ((c[1] & 0x0ff) << 8) | (c[0] & 0x0ff);
	}
#if 0
	for (int i=0; i<LEDS_RIGHT; i++) {
		cv::Vec3b c = _colorsR.at<cv::Vec3b>(cv::Point(0, i));
		_ws2811->channel[0].leds[i+LEDS_LEFT+LEDS_TOP] = ((c[2] & 0x0ff) << 16) | ((c[1] & 0x0ff) << 8) | (c[0] & 0x0ff);
	}
	for (int i=0; i<LEDS_BOTTOM; i++) {
		cv::Vec3b c = _colorsB.at<cv::Vec3b>(cv::Point(i, 0));
		_ws2811->channel[0].leds[i+LEDS_LEFT+LEDS_TOP+LEDS_RIGHT] = ((c[2] & 0x0ff) << 16) | ((c[1] & 0x0ff) << 8) | (c[0] & 0x0ff);
	}
#else	
	for (int i=0; i<LEDS_BOTTOM; i++) {
		cv::Vec3b c = _colorsB.at<cv::Vec3b>(cv::Point(i, 0));
		_ws2811->channel[1].leds[i] = ((c[2] & 0x0ff) << 16) | ((c[1] & 0x0ff) << 8) | (c[0] & 0x0ff);
	}
	for (int i=0; i<LEDS_RIGHT; i++) {
		cv::Vec3b c = _colorsR.at<cv::Vec3b>(cv::Point(0, i));
		_ws2811->channel[1].leds[i+LEDS_BOTTOM] = ((c[2] & 0x0ff) << 16) | ((c[1] & 0x0ff) << 8) | (c[0] & 0x0ff);
	}
	
#endif	
	ws2811_render(_ws2811);

}

bool AmbiPi::init(double gamma)
{
	_ws2811 = (ws2811_t *) malloc(sizeof(ws2811_t));
	memset(_ws2811, 0, sizeof(ws2811_t));

	_ws2811->freq			= WS2811_TARGET_FREQ;
	_ws2811->dmanum			= DMA;
	_ws2811->channel[0].gpionum	= GPIO_PIN1;
	_ws2811->channel[0].count	= LEDS_LEFT +  LEDS_TOP;// + LEDS_RIGHT + LEDS_BOTTOM;
	_ws2811->channel[0].strip_type	= WS2812_STRIP;
	_ws2811->channel[0].brightness	= MAX_BRIGHTNESS;
	
	_ws2811->channel[1].gpionum	= GPIO_PIN2;
	_ws2811->channel[1].count	= LEDS_BOTTOM + LEDS_RIGHT;
	_ws2811->channel[1].strip_type	= WS2812_STRIP;
	_ws2811->channel[1].brightness	= MAX_BRIGHTNESS;
	

	ws2811_return_t ret = ws2811_init(_ws2811);
	if (ret != WS2811_SUCCESS) {
		fprintf(stderr, "Error: %s\n", ws2811_get_return_t_str(ret));
		free(_ws2811);
		return false;
	}

	if (gamma != 0) {
		ws2811_set_custom_gamma_factor(_ws2811, gamma);
	}
	return true;
}

#ifdef _DEVEL_
void AmbiPi::guiDemo(cv::Mat frame)
{
	const int border = 145;
	const double alpha = 0.95;

	calculateAmbilightFromFrame(frame, alpha);

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

void AmbiPi::calculateAmbilightFromFrame(cv::Mat frame, double alpha)
{
	double dw = frame.cols / (double) LEDS_TOP;
	double dh = frame.rows / (double) LEDS_LEFT;

	int interpolation = cv::INTER_LINEAR; // INTER_CUBIC
	cv::Mat colorsTop;
	cv::resize(frame(cv::Rect(0,0,frame.cols, dh)), colorsTop, cv::Size(LEDS_TOP, 1), 0, 0, interpolation);
	cv::addWeighted(_colorsT, alpha, colorsTop, 1.0 - alpha, 0.0, _colorsT);

	cv::Mat colorsBottom;
	cv::resize(frame(cv::Rect(0, frame.rows-dh, frame.cols, dh)), colorsBottom, cv::Size(LEDS_BOTTOM, 1), 0, 0, interpolation); // INTER_CUBIC
	cv::addWeighted(_colorsB, alpha, colorsBottom, 1.0 - alpha, 0.0, _colorsB);

	cv::Mat colorsLeft;
	cv::resize(frame(cv::Rect(0,0, dw, frame.rows)), colorsLeft, cv::Size(1, LEDS_LEFT), 0, 0, interpolation);
	cv::addWeighted(_colorsL, alpha, colorsLeft, 1.0 - alpha, 0.0, _colorsL);

	cv::Mat colorsRight;
	cv::resize(frame(cv::Rect(frame.cols-dw, 0, dw, frame.rows)), colorsRight, cv::Size(1, LEDS_RIGHT), 0, 0, interpolation); // INTER_CUBIC
	cv::addWeighted(_colorsR, alpha, colorsRight, 1.0 - alpha, 0.0, _colorsR);
}
