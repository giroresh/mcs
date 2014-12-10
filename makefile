# Libraries

# TagLib - libtag/libtagc v1.7.2
TAGLIB_CFLAGS=-I/usr/include/taglib
TAGLIB_LIBS=-L/usr/lib/arm-linux-gnueabihf -ltag_c

CC=gcc
CFLAGS=-Wall -g -c
LFLAGS=-Wall -g
INCS=$(TAGLIB_CFLAGS)
LIBS=$(TAGLIB_LIBS)
TARGET=server

OBJS=mcs_taglib.o mcs.o
SRC_DIR=src

MEDIA_DIR="/mnt/usb/" "/home/pi/media/"


all: debug-dep

debug:
	$(CC) $(CFLAGS) -DMCS_DEBUG src/mcs.c
	$(CC) $(LFLAGS) mcs.o -o $(TARGET)

release:
	$(CC) -c src/mcs.c
	$(CC) mcs.o -o $(TARGET)

debug-dep:
	$(CC) $(CFLAGS) $(INCS) -DMCS_DEBUG src/mcs_taglib.c
	$(CC) $(CFLAGS) $(INCS) -DMCS_DEBUG -DMCS_TAGLIB src/mcs.c
	$(CC) $(LFLAGS) $(OBJS) -o $(TARGET) $(LIBS)

release-dep:
	$(CC) -c $(INCS) src/mcs_taglib.c
	$(CC) -c $(INCS) -DMCS_TAGLIB src/mcs.c
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)


run: $(TARGET)
	./$(TARGET) $(MEDIA_DIR)

clean:
	rm -rf *o $(TARGET)

leak:
	valgrind --tool=memcheck --leak-check=full --show-reachable=yes ./$(TARGET) $(MEDIA_DIR)

