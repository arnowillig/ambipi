#ifndef AMBIPI_H
#define AMBIPI_H

#include "rpi_ws281x/ws2811.h"
#include <opencv2/opencv.hpp>

class AmbiPi
{
public:	
	enum Mode {
		Off = 0,
		White,
		Color,
		Rainbow,
		TestPattern
	};

public:
	AmbiPi();
	virtual ~AmbiPi();
	
	void setMode(Mode m) { _mode = m; }
	Mode mode() const { return _mode; }

	bool init(double gamma);
	void setBrightness(uint8_t bri);
	void setColor(uint8_t r, uint8_t g, uint8_t  b);
	void setColorLeft(uint8_t r, uint8_t g, uint8_t  b);
	void setColorTop(uint8_t r, uint8_t g, uint8_t  b);
	void setColorBottom(uint8_t r, uint8_t g, uint8_t  b);
	void setColorRight(uint8_t r, uint8_t g, uint8_t  b);
	void drawTestPattern(int cnt, int bri);
	int ledCount() const;
	void rainbow(int cnt);

#ifdef _DEVEL_
	void guiDemo(cv::Mat frame);
#endif
	void calculateAmbilightFromFrame(cv::Mat frame, double alpha);
	void clear();
	void render();
private:
	ws2811_t* _ws2811;
	cv::Mat _colorsT;
	cv::Mat _colorsB;
	cv::Mat _colorsL;
	cv::Mat _colorsR;
	Mode _mode;

};

#endif // AMBIPI_H
