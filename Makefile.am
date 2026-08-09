SUBDIRS = icons
bin_PROGRAMS = mrwatchmaker
noinst_PROGRAMS = port_finder
port_finder_SOURCES = src/port_finder.c
port_finder_CFLAGS = -Wall -Wextra
port_finder_LDADD =
EXTRA_PROGRAMS = mrwatchmaker-dbg \
		 mrwatchmaker-prf \
		 mrwatchmaker-vlg
MOSTLYCLEANFILES = $(EXTRA_PROGRAMS)

mrwatchmaker_SOURCES = src/algo.c \
		   src/audio.c \
		   src/computer.c \
		   src/config.c \
		   src/interface.c \
		   src/output_panel.c \
		   src/serializer.c \
		   src/serial_motor.c \
		   src/serial_motor.h \
		   src/i18n.c \
		   src/i18n.h \
		   src/mrwatchmaker.h \
		   src/visual_baseline.cpp \
		   src/visual_baseline.h

mrwatchmaker_dbg_SOURCES = $(mrwatchmaker_SOURCES)
mrwatchmaker_prf_SOURCES = $(mrwatchmaker_SOURCES)
mrwatchmaker_vlg_SOURCES = $(mrwatchmaker_SOURCES)

LIBS = $(GTK_LIBS) \
       $(GTHREAD_LIBS) \
       $(PORTAUDIO_LIBS) \
       $(FFTW_LIBS) \
       $(OPENCV_LIBS) \
       -lstrmiids \
       -loleaut32 \
       -lpthread \
       -lm \
       -lstdc++

AM_CPPFLAGS = -DPROGRAM_NAME=\"MrWatchmaker\" -DVERSION=\"$(PACKAGE_VERSION)\"
mrwatchmaker_dbg_CPPFLAGS = $(AM_CPPFLAGS) -DDEBUG

AM_CFLAGS = $(GTK_CFLAGS) \
	    $(GTHREAD_CFLAGS) \
	    $(PORTAUDIO_CFLAGS) \
	    $(FFTW_CFLAGS) \
	    $(OPENCV_CFLAGS)
AM_CXXFLAGS = $(AM_CFLAGS)
mrwatchmaker_dbg_CFLAGS = $(AM_CFLAGS) -ggdb
mrwatchmaker_prf_CFLAGS = $(AM_CFLAGS) -pg
mrwatchmaker_vlg_CFLAGS = $(AM_CFLAGS) -g

mrwatchmaker_LDFLAGS = $(AM_LDFLAGS)
mrwatchmaker_dbg_LDFLAGS = $(AM_LDFLAGS)
mrwatchmaker_prf_LDFLAGS = $(AM_LDFLAGS)
mrwatchmaker_vlg_LDFLAGS = $(AM_LDFLAGS)

if BE_WINDOWS
if HAVE_WINDRES
mrwatchmaker_SOURCES += icons/tg-timer.rc
endif
mrwatchmaker_LDFLAGS += -mwindows
port_finder_LDFLAGS = -mconsole
mrwatchmaker_dbg_LDFLAGS += -mconsole
mrwatchmaker_prf_LDFLAGS += -mconsole
mrwatchmaker_vlg_LDFLAGS += -mconsole
endif

EXTRA_DIST = autogen.sh \
	     icons \
	     packaging \
	     LICENSE \
	     README.md \
	     reference_9oclock.png

.rc.o:
	$(WINDRES) $< -O coff -o $@

test: mrwatchmaker-dbg
	./mrwatchmaker-dbg test
.PHONY: test

valgrind: mrwatchmaker-vlg
	valgrind --leak-check=full -v --num-callers=99 --suppressions=.valgrind.supp ./$^
.PHONY: valgrind

# 빌드 후 exe 옆에 런타임 DLL 복사 (c:/mrwatchmaker)
copy-dlls: all
	@bash packaging/copy_dlls_mingw.sh "$(CURDIR)"
.PHONY: copy-dlls
