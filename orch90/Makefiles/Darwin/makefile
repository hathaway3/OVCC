TARGET = liborch90.so
 
CFLAGS += -fPIC
LDFLAGS += -shared

_OBJS = orch90.o fileops.o

include ../Makefile.common

$(ODIR)/orch90.o: orch90.c orch90.h
$(ODIR)/fileops.o: ../CoCo/fileops.c ../CoCo/fileops.h

install:
	mkdir -p ../ovcc.app/Contents/PlugIns
	mkdir -p ../ovcc.app/Contents/Frameworks
	install -m 644 $(TARGET) ../ovcc.app/Contents/PlugIns/$(TARGET)
	dylibbundler -of -b -p @loader_path/../Frameworks/ -x ../ovcc.app/Contents/PlugIns/$(TARGET) -d ../ovcc.app/Contents/Frameworks -s $(BREW_PREFIX)/lib -s /usr/local/lib

test:
