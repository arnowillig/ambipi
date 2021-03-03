QT =

CONFIG += c++17 console
CONFIG -= app_bundle

MAKEFILE = Makefile_qmake
OTHER_FILES += Makefile html/*

DEFINES += _GUI_

SOURCES += \
	main.cpp \
	ambipi.cpp \
	framebuffer.cpp \
	restserver.cpp \
	rpi_ws281x/dma.c rpi_ws281x/mailbox.c rpi_ws281x/pcm.c rpi_ws281x/pwm.c rpi_ws281x/rpihw.c rpi_ws281x/ws2811.c

HEADERS += \
	ambipi.h \
	framebuffer.h \
	restserver.h \
	rpi_ws281x/clk.h rpi_ws281x/dma.h rpi_ws281x/gpio.h rpi_ws281x/mailbox.h rpi_ws281x/pcm.h rpi_ws281x/pwm.h rpi_ws281x/rpihw.h rpi_ws281x/ws2811.h


INCLUDEPATH += /usr/include/opencv4/

LIBS += -lopencv_core -lopencv_imgproc -lopencv_videoio -lopencv_imgproc -lopencv_imgcodecs -lpthread -lopencv_calib3d
LIBS += -lopencv_highgui
LIBS += -lpistache -lssl -lcrypto


qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
