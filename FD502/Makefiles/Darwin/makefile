TARGET = libfd502.so

CFLAGS += -fpic
LDFLAGS += -shared

_OBJS = fd502.o distortc.o wd1793.o fileops.o iniman.o

include ../Makefile.common

$(ODIR)/fd502.o: fd502.c fd502.h resource.h distortc.h wd1793.h ../CoCo/fileops.h
$(ODIR)/distortc.o: distortc.c distortc.h
$(ODIR)/wd1793.o: wd1793.c wd1793.h fd502.h defines.h
$(ODIR)/fileops.o: ../CoCo/fileops.c ../CoCo/fileops.h
$(ODIR)/iniman.o: ../CoCo/iniman.c ../CoCo/iniman.h

install:
	mkdir -p ../ovcc.app/Contents/PlugIns
	mkdir -p ../ovcc.app/Contents/Frameworks
	install -m 644 $(TARGET) ../ovcc.app/Contents/PlugIns/$(TARGET)
	dylibbundler -of -b -p @loader_path/../Frameworks/ -x ../ovcc.app/Contents/PlugIns/$(TARGET) -d ../ovcc.app/Contents/Frameworks -s $(BREW_PREFIX)/lib -s /usr/local/lib

test:
