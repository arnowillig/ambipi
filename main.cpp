#include <stdio.h>
#include "ambipi.h"

int main(int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	fprintf(stderr, "AmbiPi\n");
	AmbiPi ambiPi;
	if (!ambiPi.init(0)) {
#ifndef _DEVEL_
		return 1;
#endif
	}

	cv::VideoCapture* capture = new cv::VideoCapture("/home/akw/Downloads/big_buck_bunny_1080p_surround.avi");

#ifdef _DEVEL_
	cv::Mat inputFrame;
	while (true) {
		capture->grab();
		capture->retrieve(inputFrame);

		cv::resize(inputFrame, inputFrame, cv::Size(0,0), 0.5, 0.5, cv::INTER_LINEAR);
		ambiPi.guiDemo(inputFrame);

		if  (cv::waitKey(10)=='q') {
			break;
		}
	}
#endif
	return 0;
}
