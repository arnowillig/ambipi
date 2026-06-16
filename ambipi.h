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
		LeftSide,
		RightSide,
		Vegas,
		TestPattern,
		AmbiLight,
		AmbiLight2,
		Knightrider
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
	
	void setMode(Mode m) { std::lock_guard<std::recursive_mutex> lock(_mutex); _mode = m; }
	Mode mode() const { std::lock_guard<std::recursive_mutex> lock(_mutex); return _mode; }

	bool init(double gamma);
	void clearLastFrame(uint8_t r,uint8_t g,uint8_t b);

	int ledCount() const;

	void setBrightness(uint8_t bri);
	uint8_t brightness() const;

	void setAlpha(double alpha);
	double alpha() const;

	void setGamma(double gamma);
	double gamma() const;
	
	void setUpdateCropRect(bool cropping);
	bool croppingEnabled() const;
	
	void updateCropRect(cv::Mat frame);

	void setColor(uint8_t r, uint8_t g, uint8_t  b);
	void setColor(int idx, uint8_t r, uint8_t g, uint8_t  b);
	void setColorLeft(uint8_t r, uint8_t g, uint8_t  b);
	void setColorTop(uint8_t r, uint8_t g, uint8_t  b);
	void setColorBottom(uint8_t r, uint8_t g, uint8_t  b);
	void setColorRight(uint8_t r, uint8_t g, uint8_t  b);

	void setColorLeft(uint8_t idx, uint8_t r, uint8_t g, uint8_t  b);
	void setColorTop(uint8_t idx, uint8_t r, uint8_t g, uint8_t  b);
	void getColorTop(uint8_t idx, uint8_t* r, uint8_t* g, uint8_t*  b);
	void setColorBottom(uint8_t idx, uint8_t r, uint8_t g, uint8_t  b);
	void setColorRight(uint8_t idx, uint8_t r, uint8_t g, uint8_t  b);
	
	void getRainbowColor(int pos, uint8_t& r, uint8_t& g, uint8_t& b);

	int drawTestPattern(int cnt);
	int rainbow(int cnt);
	int vegas(int cnt);
	int knightrider(int cnt);
	int goal(int cnt, bool leftSide);

	cv::Mat getDebugFrame(cv::Mat frame) const;
#ifdef _GUI_
	void drawGUI(cv::Mat frame);
#endif
	cv::Mat cropBorders(cv::Mat frame, bool debug) const;
	void calculateGameWallFrameFromFrame(cv::Mat frame);
	void calculateDisplayFrameFromFrame(cv::Mat frame);
	void calculateAmbilightFromFrame(cv::Mat frame, bool bgr=false);
	void calculateKickerLightsFromFrame(cv::Mat frame);
	void clear();
	void render();
	cv::Mat createTestImage(int w, int h);
	
	void setLastFrame(cv::Mat frame);
	cv::Mat lastFrame() const;
	bool getEnableDisplayVideo() const;
	void setEnableDisplayVideo(bool enableDisplayVideo);
	bool getEnableGameWallAmbilight() const;
	void setEnableGameWallAmbilight(bool enable);
	bool getEnableGamingTable() const;
	void setEnableGamingTable(bool enableGamingTable);
	bool getSwapRB() const;
	void setSwapRB(bool swap);
	void fadeColors(float pct);
private:
	ws2811_t* _ws2811;
	FrameBuffer* _fb;
	cv::Mat _colorsT;
	cv::Mat _colorsB;
	cv::Mat _colorsL;
	cv::Mat _colorsR;
	cv::Rect _cropRect;
	Mode _mode;
	double _alpha;
	double _gamma;
	mutable std::recursive_mutex _mutex;
	cv::Mat _lastFrame;
	bool _enableCropping;
	bool _enableDisplayVideo;
	bool _enableGameWallAmbilight;
	bool _enableGamingTable;
	bool _swapRB;
	void loadSettings();
	void saveSettings() const;
	std::vector<uint8_t> _lut;
	
	std::vector<uint8_t> lastFrame_;
	void unpackRgb(uint32_t packed, uint8_t &r, uint8_t &g, uint8_t &b);
	bool sendKDPDatagram(const uint8_t *data, size_t size);
	bool sendFullFrame(cv::Mat frame);
	void sendFrameToGameWall(const cv::Mat& resized6x4BGR);
};

#endif // AMBIPI_H
