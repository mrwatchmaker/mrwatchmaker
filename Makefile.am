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
		   src/tg.h

mrwatchmaker_dbg_SOURCES = $(mrwatchmaker_SOURCES)
mrwatchmaker_prf_SOURCES = $(mrwatchmaker_SOURCES)
mrwatchmaker_vlg_SOURCES = $(mrwatchmaker_SOURCES)

LIBS = $(GTK_LIBS) \
       $(GTHREAD_LIBS) \
       $(PORTAUDIO_LIBS) \
       $(FFTW_LIBS) \
       -lpthread \
       -lm

AM_CPPFLAGS = -DPROGRAM_NAME=\"MrWatchmaker\" -DVERSION=\"$(PACKAGE_VERSION)\"
mrwatchmaker_dbg_CPPFLAGS = $(AM_CPPFLAGS) -DDEBUG

AM_CFLAGS = $(GTK_CFLAGS) \
	    $(GTHREAD_CFLAGS) \
	    $(PORTAUDIO_CFLAGS) \
	    $(FFTW_CFLAGS)
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

desktopdir = $(datadir)/applications
dist_desktop_DATA = icons/tg-timer.desktop

mimedir = $(datadir)/mime/packages
dist_mime_DATA = icons/tg-timer.xml

dist_man_MANS = docs/tg-timer.1

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

# 회사 가져갈 폴더: mrwatchmaker.exe + README 등
OFFICE_DIR = 회사가져갈_MrWatchmaker
office: all
	@rm -rf $(OFFICE_DIR) && mkdir -p $(OFFICE_DIR)
	@cp -f mrwatchmaker$(EXEEXT) $(OFFICE_DIR)/mrwatchmaker$(EXEEXT) 2>/dev/null || true
	@cp -f reference_9oclock.png $(OFFICE_DIR)/ 2>/dev/null || true
	@cp -f README.md $(OFFICE_DIR)/ 2>/dev/null || cp -f ../README.md $(OFFICE_DIR)/ 2>/dev/null || true
	@cp -f LICENSE $(OFFICE_DIR)/ 2>/dev/null || cp -f ../LICENSE $(OFFICE_DIR)/ 2>/dev/null || true
	@test -f port.conf && cp -f port.conf $(OFFICE_DIR)/ || echo "5" > $(OFFICE_DIR)/port.conf
	@echo "MrWatchmaker 회사용 폴더" > $(OFFICE_DIR)/README_회사용.txt
	@echo "1. mrwatchmaker.exe 실행" >> $(OFFICE_DIR)/README_회사용.txt
	@echo "2. port.conf 에 COM 번호만 적기 (예: 5 = COM5)" >> $(OFFICE_DIR)/README_회사용.txt
	@echo "완료: $(OFFICE_DIR)/"
.PHONY: office
