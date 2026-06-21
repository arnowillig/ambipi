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
LIBS += -lssl -lcrypto
LIBS += -lsqlite3   # Becker Centronic shutter counter DB (centronic.cpp)

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

# --- Cross-build a .deb for the Pi (armhf) via Docker, and deploy ----------
# Mirrors the flow-grid setup. armhf runs under QEMU emulation on this host.
DOCKER_IMAGE := ambipi-cross-armhf
DOCKER_PLAT  := linux/arm/v7
DEB_ARCH     := armhf
DEB_VERSION  ?= 1.0.27
DEPLOY_HOST  ?= pi@ataripi.local

.PHONY: deb deploy docker-clean

deb:
	docker build --platform $(DOCKER_PLAT) -t $(DOCKER_IMAGE) -f Dockerfile.cross .
	docker run --rm --platform $(DOCKER_PLAT) \
		-v "$(CURDIR)":/src:delegated -w /src \
		-e DEB_VERSION=$(DEB_VERSION) -e DEB_ARCH=$(DEB_ARCH) \
		$(DOCKER_IMAGE) bash packaging/build-deb.sh
	@ls -lh dist/*.deb

# Fail fast (don't hang) if the Pi is offline/unreachable, and auto-accept a
# new host key so a first-time connection doesn't block on a yes/no prompt.
SSH_OPTS := -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new

deploy: deb
	@DEB=$$(ls -t dist/*.deb | head -1) && \
		echo "--- Deploying $$DEB to $(DEPLOY_HOST) ---" && \
		scp $(SSH_OPTS) "$$DEB" $(DEPLOY_HOST):/tmp/ambipi.deb && \
		ssh $(SSH_OPTS) $(DEPLOY_HOST) 'sudo dpkg -i /tmp/ambipi.deb || sudo apt-get -f install -y; rm -f /tmp/ambipi.deb; dpkg -s ambipi >/dev/null 2>&1' && \
		echo "--- Deployed; service restarted by postinst ---"

docker-clean:
	docker rmi $(DOCKER_IMAGE) 2>/dev/null || true
