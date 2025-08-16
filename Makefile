SOURCES = $(wildcard *.cpp)
HEADERS = $(wildcard *.h)
OBJECTS = $(SOURCES:.cpp=.o)

# --- OpenCV via pkg-config ---
OPENCV_CFLAGS := $(shell pkg-config --cflags opencv4 2>/dev/null || pkg-config --cflags opencv)
OPENCV_LIBS   := $(shell pkg-config --libs   opencv4 2>/dev/null || pkg-config --libs   opencv)

# --- Includes / flags ---
INCLUDES += -I pistache/include/ -I /opt/vc/include
CXXFLAGS += -Wfatal-errors -Wno-deprecated-declarations -std=c++2a $(INCLUDES) $(OPENCV_CFLAGS)
# pthread on both compile & link
CXXFLAGS += -pthread

# --- Libraries ---
LIBS += rpi_ws281x/build/libws2811.a
LIBS += pistache/build/src/libpistache.a
LIBS += $(OPENCV_LIBS) -lpthread

TARGET = ambipi

# Dispmanx (kept as-is)
ifneq ("$(wildcard /opt/vc/lib/libbcm_host.so)","")
    HAVE_DISPMANX = 1
    LIBS += /opt/vc/lib/libbcm_host.so
else
    HAVE_DISPMANX = 0
endif

CXXFLAGS += -DHAVE_DISPMANX=$(HAVE_DISPMANX)

# Filesystem lib only needed on very old GCC; keep if you still need it
LIBS += -lstdc++fs

all: $(TARGET)

%.o: %.cpp $(HEADERS)
	@echo "Compiling $@ ..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): rpi_ws281x/build/libws2811.a pistache/build/src/libpistache.a $(OBJECTS)
	@echo "Linking $@ ..."
	@$(CXX) -Wfatal-errors -o $(TARGET) $(OBJECTS) $(LIBS)

rpi_ws281x/build/libws2811.a:
	@mkdir -p rpi_ws281x/build
	@cd rpi_ws281x/build && cmake ..
	@$(MAKE) -C rpi_ws281x/build

pistache/build/src/libpistache.a:
	@mkdir -p pistache/build
	@cd pistache/build && cmake -DPISTACHE_BUILD_TESTS=OFF ..
	@$(MAKE) -C pistache/build

clean:
	@rm -rf $(TARGET) *.a *.o *~

distclean: clean
	@rm -rf rpi_ws281x/build pistache/build

run: ambipi
	sudo ./ambipi

restart: $(TARGET)
	sudo service ambipi restart

log: $(TARGET)
	sudo journalctl --vacuum-time=1s -u ambipi.service
	sudo journalctl --rotate  -u ambipi.service
	sudo journalctl -u ambipi.service
