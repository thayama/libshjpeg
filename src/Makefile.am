#
#
#
lib_LTLIBRARIES = libshjpeg.la

include_HEADERS = \
	shjpeg.h \
	shjpeg_types.h 

libshjpeg_la_LIBADD = $(LIBS)
libshjpeg_la_LDFLAGS = -version-info $(LIBSHJPEG_VERSION)

libshjpeg_la_SOURCES = \
	shjpeg_common.c \
	shjpeg_config.c \
	shjpeg_jpu.c \
	shjpeg_decode.c \
	shjpeg_encode.c \
	shjpeg_internal.h \
	shjpeg_utils.h \
	shjpeg_regs.h \
	shjpeg_jpu.h
