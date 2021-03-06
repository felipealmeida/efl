MAINTAINERCLEANFILES = Makefile.in 

.PHONY: doc

PACKAGE_DOCNAME = $(PACKAGE_TARNAME)-$(PACKAGE_VERSION)-doc

if EFL_BUILD_DOC

doc-clean:
	rm -rf html/ latex/ man/ xml/ $(top_builddir)/$(PACKAGE_DOCNAME).tar*

doc: all
	$(efl_doxygen)
	cp $(srcdir)/img/* html/
	cp $(srcdir)/img/*.eps latex/
	rm -rf $(PACKAGE_DOCNAME).tar*
	mkdir -p $(PACKAGE_DOCNAME)/doc
	cp -R html/ latex/ man/ $(PACKAGE_DOCNAME)/doc
	tar cf $(PACKAGE_DOCNAME).tar $(PACKAGE_DOCNAME)/
	bzip2 -9 $(PACKAGE_DOCNAME).tar
	rm -rf $(PACKAGE_DOCNAME)/
	mv $(PACKAGE_DOCNAME).tar.bz2 $(top_builddir)
	@echo "Documentation Package: doc/$(PACKAGE_DOCNAME).tar.bz2"
	@echo "Documentation HTML: doc/html"

clean-local: doc-clean

else

doc:
	@echo "Documentation not built. Run ./configure --help"

endif

EXTRA_DIST = \
Doxyfile.in \
e.css \
head.html \
foot.html \
ecore_examples.dox \
eldbus_examples.dox \
edje_examples.dox \
eet_examples.dox \
eina_examples.dox \
eio_examples.dox \
emotion_examples.dox \
eo_tutorial.dox \
ephysics_examples.dox \
evas_examples.dox \
main.dox \
pkgconfig.dox

include images.mk

all-local:
	@rm -f images.tmp; \
	echo $(ECHO_N) 'EXTRA_DIST += ' > images.tmp; \
	(cd $(srcdir) && find ./img -type f -print | cut -d/ -f2- | grep -v '~' | sort | tr '\n' ' ') >> images.tmp && \
	echo >> images.tmp && \
	if ! cmp "$(srcdir)/images.mk" images.tmp >/dev/null; then \
	   echo "warning: new images added to '$(srcdir)/images.mk'"; \
	   mv -f images.tmp "$(srcdir)/images.mk"; \
	else \
	   rm images.tmp; \
	fi
