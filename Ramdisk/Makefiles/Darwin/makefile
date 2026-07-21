TARGET = libramdisk.so
 
CFLAGS += -fPIC
LDFLAGS += -shared

_OBJS = ramdisk.o memboard.o

include ../Makefile.common

$(ODIR)/ramdisk.o: ramdisk.c memboard.h
$(ODIR)/memboard.o: memboard.c memboard.h

install:
	mkdir -p ../ovcc.app/Contents/PlugIns
	mkdir -p ../ovcc.app/Contents/Frameworks
	install -m 644 $(TARGET) ../ovcc.app/Contents/PlugIns/$(TARGET)
	dylibbundler -of -b -p @loader_path/../Frameworks/ -x ../ovcc.app/Contents/PlugIns/$(TARGET) -d ../ovcc.app/Contents/Frameworks -s $(BREW_PREFIX)/lib -s /usr/local/lib

test:
