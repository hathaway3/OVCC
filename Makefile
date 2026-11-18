TOS:=$(shell uname -s)
export TARGETOS = $(TOS)

DIRS = CoCo becker FD502 HardDisk mpi orch90 Ramdisk SuperIDE

ifeq ($(TARGETOS),Linux)
DIRS := $(DIRS) mpu
endif

ifeq ($(TARGETOS),Darwin)
DIRS := $(DIRS) mpu
endif

.PHONY: all subdirs $(DIRS)

all: subdirs

subdirs: $(DIRS)

$(DIRS):
	$(MAKE) -C $@ -f Makefiles/$(TARGETOS)/makefile $(ACTION)

install: ACTION = install
install: subdirs
ifeq ($(TARGETOS),Darwin)
	# Re-sign after all PlugIns/Frameworks are installed (CoCo's install runs first),
	# then bump the bundle's own mtime so Finder/LaunchServices refreshes metadata.
	codesign --force --deep -s - ovcc.app
	touch ovcc.app
endif

clean: ACTION = clean
clean: subdirs
