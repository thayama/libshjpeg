#
#
#
#
AM_CFLAGS = -Wall -Werror
AM_CPPFLAGS = -I$(srcdir)/../src

bin_PROGRAMS = shjpegtest v2mjpeg shjpegshow

shjpegtest_SOURCES = shjpegtest.c
shjpegtest_LDADD = ../src/libshjpeg.la

v2mjpeg_SOURCES = v2mjpeg.c
v2mjpeg_LDADD = ../src/libshjpeg.la

shjpegshow_SOURCES = shjpegshow.c
shjpegshow_LDFLAGS = -L/usr/local/lib
shjpegshow_CFLAGS = -I/usr/local/include -I/usr/local/include/directfb -I/usr/local/include/directfb-internal/
shjpegshow_LDADD = ../src/libshjpeg.la -ldirectfb -lfusion -ldirect -lpthread -lz

