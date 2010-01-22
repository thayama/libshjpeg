docdir=$(prefix)/share/doc/@PACKAGE@

man_MANS = shjpegshow.1 shjpegtest.1 v2mjpeg.1

EXTRA_DIST = $(man_MANS) Doxyfile.in

# ensure doxygen-build.stamp is included,  or else the documentation will
# not be built or distributed
noinst_DATA = doxygen-build.stamp

if HAVE_DOXYGEN
#doxygen-build.stamp: Doxyfile $(top_srcdir)/include/shjpeg/*.h $(top_srcdir)/src/examples/*.c
doxygen-build.stamp: Doxyfile $(top_srcdir)/include/shjpeg/*.h
	doxygen
	touch doxygen-build.stamp
else
doxygen-build.stamp:
	echo "*** Warning: Doxygen not found; documentation will not be built."
	touch doxygen-build.stamp
endif

dist_docdir = $(distdir)/libshjpeg

dist-hook:
	if test -d libshjpeg; then \
	  mkdir $(dist_docdir); \
	  for dir in libshjpeg/*; do \
            b=`basename $$dir`; \
            if test $$b != "CVS"; then \
              if test -d $$dir; then \
                mkdir $(dist_docdir)/$$b; \
	        for f in $$dir/*; do \
                  cp -p $$f $(dist_docdir)/$$b; \
                done \
              fi \
            fi \
          done \
	fi


install-data-local: doxygen-build.stamp
	$(mkinstalldirs) $(DESTDIR)$(docdir)
	if test -d libshjpeg; then \
	  for dir in libshjpeg/*; do \
	    if test -d $$dir; then \
	      b=`basename $$dir`; \
	      $(mkinstalldirs) $(DESTDIR)$(docdir)/$$b; \
	      for f in $$dir/*; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)/$$b; \
              done \
	    fi \
	  done \
	fi

uninstall-local:
	rm -rf $(DESTDIR)$(docdir)

clean-local:
	if test -d libshjpeg; then rm -rf libshjpeg; fi
	if test -f doxygen-build.stamp; then rm -f doxygen-build.stamp; fi
