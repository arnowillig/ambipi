
OBJECTS = main.o ambipi.o

LIBS = rpi_ws281x/build/libws2811.a -lpthread
LIBS += -lopencv_core -lopencv_imgproc -lopencv_videoio -lopencv_imgcodecs -lpthread 
# -lopencv_calib3d

TARGET = ambipi

$(TARGET): rpi_ws281x/build/libws2811.a $(OBJECTS)
	$(CXX) -Wfatal-errors -o $(TARGET)   $(OBJECTS) $(LIBS)

rpi_ws281x/build/libws2811.a:
	cd rpi_ws281x; mkdir build; cd build; cmake ..
	$(MAKE) -C rpi_ws281x/build

%.o: %.cpp %.h
	$(CXX) -Wfatal-errors $(CFLAGS) -c $<

clean:
	@rm -rf $(TARGET) *.a *.o *~ rpi_ws281x/build


run:	ambipi
	sudo ./ambipi

	