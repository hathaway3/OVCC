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
	# Bumps the bundle directory's own mtime so Finder/LaunchServices notices
	# the bundle changed and refreshes its cached icon/Info.plist metadata --
	# without this, a rebuilt app can keep showing a stale icon or version in
	# Finder even though Contents/ was just updated by the module installs above.
	touch ovcc.app
endif

clean: ACTION = clean
clean: subdirs
