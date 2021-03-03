#ifndef AMBIPI_H
#define AMBIPI_H

#include <opencv2/opencv.hpp>
#include <mutex>

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

	int ledCount() const;

	void setBrightness(uint8_t bri);
	uint8_t brightness() const;

	void setAlpha(double alpha);
	double alpha() const;

	void setGamma(double gamma);
	double gamma() const;
	
	void setEnableCropping(bool cropping);
	bool croppingEnabled() const;

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
	void rainbow(int cnt);

#ifdef _GUI_
	void drawGUI(cv::Mat frame);
#endif
	cv::Mat cropBorders(cv::Mat frame, bool debug) const;
	void calculateAmbilightFromFrame(cv::Mat frame, bool bgr=false);
	void clear();
	void render();
	cv::Mat createTestImage(int w, int h);
	
	void setLastFrame(cv::Mat frame);
	cv::Mat lastFrame() const;
private:
	ws2811_t* _ws2811;
	FrameBuffer* _fb;
	cv::Mat _colorsT;
	cv::Mat _colorsB;
	cv::Mat _colorsL;
	cv::Mat _colorsR;
	Mode _mode;
	double _alpha;
	double _gamma;
	mutable std::mutex _mutex;
	cv::Mat _lastFrame;
	bool _enableCropping;
};

#endif // AMBIPI_H
