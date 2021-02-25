#ifndef AMBIPI_H
#define AMBIPI_H

#include "rpi_ws281x/ws2811.h"
#include <opencv2/opencv.hpp>

class AmbiPi
{
	ws2811_t* _ws2811;
	cv::Mat _colorsT;
	cv::Mat _colorsB;
	cv::Mat _colorsL;
	cv::Mat _colorsR;

public:
	AmbiPi();
	virtual ~AmbiPi();

	bool init(double gamma);
public:
#ifdef _DEVEL_
	void guiDemo(cv::Mat frame);
#endif
	void calculateAmbilightFromFrame(cv::Mat frame, double alpha);
	void clear();
	void render();
};

#endif // AMBIPI_H
