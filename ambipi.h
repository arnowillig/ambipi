#ifndef AMBIPI_H
#define AMBIPI_H

#include <opencv2/opencv.hpp>

class FrameBuffer;

struct ws2811_t;

class AmbiPi
{
public:	
	enum Mode {
		Off = 0,
		White,
		Color,
		Rainbow,
		TestPattern,
		AmbiLight,
		AmbiLight2
	};
	enum Side {
		Left = 0,
		Top,
		Right,
		Bottom
	};

public:
	AmbiPi();
	virtual ~AmbiPi();
	
	void setFrameBuffer(FrameBuffer* fb) { _fb = fb; }
	FrameBuffer* frameBuffer() const { return _fb; }
	
	void setMode(Mode m) { _mode = m; }
	Mode mode() const { return _mode; }

	bool init(double gamma);
	void setBrightness(uint8_t bri);
	void setAlpha(double alpha);
	void setGamma(double gamma);
	uint8_t getBrightness() const;
	void setColor(uint8_t r, uint8_t g, uint8_t  b);
	void setColorLeft(uint8_t r, uint8_t g, uint8_t  b);
	void setColorTop(uint8_t r, uint8_t g, uint8_t  b);
	void setColorBottom(uint8_t r, uint8_t g, uint8_t  b);
	void setColorRight(uint8_t r, uint8_t g, uint8_t  b);

	void setColorLeft(uint8_t idx, uint8_t r, uint8_t g, uint8_t  b);
	void setColorTop(uint8_t idx, uint8_t r, uint8_t g, uint8_t  b);
	void setColorBottom(uint8_t idx, uint8_t r, uint8_t g, uint8_t  b);
	void setColorRight(uint8_t idx, uint8_t r, uint8_t g, uint8_t  b);

	void drawTestPattern(int cnt, int bri);
	int ledCount() const;
	void rainbow(int cnt);

#ifdef _GUI_
	void drawGUI(cv::Mat frame);
#endif
	void calculateAmbilightFromFrame(cv::Mat frame, bool bgr=false);
	void clear();
	void render();
	cv::Mat createTestImage(int w, int h);
private:
	ws2811_t* _ws2811;
	FrameBuffer* _fb;
	cv::Mat _colorsT;
	cv::Mat _colorsB;
	cv::Mat _colorsL;
	cv::Mat _colorsR;
	Mode _mode;
	double _alpha;

};

#endif // AMBIPI_H
