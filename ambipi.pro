QT =

CONFIG += c++11 console
CONFIG -= app_bundle

MAKEFILE=Makefile_qmake



SOURCES += \
        ambipi.cpp \
        main.cpp



# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

HEADERS += \
    ambipi.h
