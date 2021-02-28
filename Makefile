
SOURCES = $(wildcard *.cpp)
OBJECTS = $(SOURCES:.cpp=.o)  


#OBJECTS = main.o ambipi.o

# WS281x
LIBS += rpi_ws281x/build/libws2811.a
# OpenCV
LIBS += -lopencv_core -lopencv_imgproc -lopencv_videoio -lopencv_imgcodecs -lpthread 
# Pistache
LIBS += pistache/build/src/libpistache.a

INCLUDES += -I pistache/include/

# -lopencv_calib3d

TARGET = ambipi

$(TARGET): rpi_ws281x/build/libws2811.a pistache/build/src/libpistache.a $(OBJECTS)
	$(CXX) -Wfatal-errors -o $(TARGET)   $(OBJECTS) $(LIBS)

ambipi.o: ambipi.cpp ambipi.h
	$(CXX) $(INCLUDES) -c $< -o $@

main.o: main.cpp
	$(CXX) $(INCLUDES) -std=c++17 -c $< -o $@

%.o: %.cpp $(INCLUDES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@


rpi_ws281x/build/libws2811.a:
	cd rpi_ws281x; mkdir build; cd build; cmake ..
	$(MAKE) -C rpi_ws281x/build

pistache/build/src/libpistache.a:
	cd pistache; mkdir build; cd build; cmake ..
	$(MAKE) -C pistache/build

#%.o: %.cpp %.h
#	$(CXX) -Wfatal-errors $(CFLAGS) $(INCLUDES) -c $<

clean:
	@rm -rf $(TARGET) *.a *.o *~

distclean: clean
	@rm -rf rpi_ws281x/build pistache/build

run:	ambipi
	sudo ./ambipi

	