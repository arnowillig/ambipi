
SOURCES = $(wildcard *.cpp)
HEADERS = $(wildcard *.h)
OBJECTS = $(SOURCES:.cpp=.o)  

LIBS += rpi_ws281x/build/libws2811.a
LIBS += pistache/build/src/libpistache.a
LIBS += -lopencv_core -lopencv_imgproc -lopencv_videoio -lopencv_imgcodecs -lpthread 

INCLUDES += -I pistache/include/ -I /opt/vc/include -I /usr/include/opencv4/

CFLAGS += -Wfatal-errors -Wno-deprecated-declarations

TARGET = ambipi

ifneq ("$(wildcard /opt/vc/lib/libbcm_host.so)","")
	HAVE_DISPMANX = 1
	LIBS += /opt/vc/lib/libbcm_host.so
else
	HAVE_DISPMANX = 0
endif

CFLAGS += -DHAVE_DISPMANX=$(HAVE_DISPMANX)

all: $(TARGET)

%.o: %.cpp $(HEADERS)
	@echo "Compiling $@ ..."
	$(CXX) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): rpi_ws281x/build/libws2811.a pistache/build/src/libpistache.a $(OBJECTS)
	@echo "Linking $@ ..."
	@$(CXX) -Wfatal-errors -o $(TARGET)   $(OBJECTS) $(LIBS)

rpi_ws281x/build/libws2811.a:
	@cd rpi_ws281x; mkdir build; cd build; cmake ..
	@$(MAKE) -C rpi_ws281x/build

pistache/build/src/libpistache.a:
	@cd pistache; mkdir build; cd build; cmake ..
	@$(MAKE) -C pistache/build

clean:
	@rm -rf $(TARGET) *.a *.o *~

distclean: clean
	@rm -rf rpi_ws281x/build pistache/build

run:	ambipi
	sudo ./ambipi

	
