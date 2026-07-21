TARGET=libsuperide.so
 
PTHREAD=-pthread

CFLAGS += -fPIC
LDFLAGS += -shared $(PTHREAD)

_OBJS = superide.o cloud9.o fileops.o idebus.o logger.o iniman.o

include ../Makefile.common

$(ODIR)/superide.o: superide.c superide.h defines.h idebus.h cloud9.h logger.h ../CoCo/fileops.h
$(ODIR)/cloud9.o: cloud9.c cloud9.h 
$(ODIR)/fileops.o: ../CoCo/fileops.c ../CoCo/fileops.h
$(ODIR)/idebus.o: idebus.c idebus.h logger.h 
$(ODIR)/logger.o: logger.c logger.h 
$(ODIR)/iniman.o: ../CoCo/iniman.c ../CoCo/iniman.h

install:
	mkdir -p ../ovcc.app/Contents/PlugIns
	mkdir -p ../ovcc.app/Contents/Frameworks
	install -m 644 $(TARGET) ../ovcc.app/Contents/PlugIns/$(TARGET)
	dylibbundler -of -b -p @loader_path/../Frameworks/ -x ../ovcc.app/Contents/PlugIns/$(TARGET) -d ../ovcc.app/Contents/Frameworks -s $(BREW_PREFIX)/lib -s /usr/local/lib

test:
