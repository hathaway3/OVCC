TARGET = libbecker.so
 
CFLAGS += -fPIC
LDFLAGS += -shared

_OBJS =	becker.o iniman.o fileops.o

include ../Makefile.common

$(ODIR)/becker.o: becker.c becker.h
$(ODIR)/iniman.o: ../CoCo/iniman.c ../CoCo/iniman.h
$(ODIR)/fileops.o: ../CoCo/fileops.c ../CoCo/fileops.h

install:
	mkdir -p ../ovcc.app/Contents/PlugIns
	mkdir -p ../ovcc.app/Contents/Frameworks
	install -m 644 $(TARGET) ../ovcc.app/Contents/PlugIns/$(TARGET)
	dylibbundler -of -b -p @loader_path/../Frameworks/ -x ../ovcc.app/Contents/PlugIns/$(TARGET) -d ../ovcc.app/Contents/Frameworks -s $(BREW_PREFIX)/lib -s /usr/local/lib

test:
