TARGET=libmpi.so
 
CFLAGS += -fPIC
LDFLAGS += -shared

_OBJS = mpi.o fileops.o iniman.o

include ../Makefile.common

$(ODIR)/mpi.o: mpi.c mpi.h ../CoCo/fileops.h
$(ODIR)/fileops.o: ../CoCo/fileops.c ../CoCo/fileops.h
$(ODIR)/iniman.o: ../CoCo/iniman.c ../CoCo/iniman.h

install:
	mkdir -p ../ovcc.app/Contents/PlugIns
	mkdir -p ../ovcc.app/Contents/Frameworks
	install -m 644 $(TARGET) ../ovcc.app/Contents/PlugIns/$(TARGET)
	dylibbundler -of -b -p @loader_path/../Frameworks/ -x ../ovcc.app/Contents/PlugIns/$(TARGET) -d ../ovcc.app/Contents/Frameworks -s $(BREW_PREFIX)/lib -s /usr/local/lib

test:
