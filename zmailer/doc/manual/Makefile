#
# GNU-Makefile for ZMailer manual
#

VERSION = "v1.99.26"

#
#  Utilizing DocBook 3.1, Structured Stylesheets, DSSSL, openjade-1.3.1
#
#  All relevant bits are available at for example in redhat 7.1 beta
#  as RPM packages (February 2001)
#
#  When your "make pdf" run complains about running out of some resource
#  in TeX, go and edit file:  /usr/share/texmf/web2c/texmf.cnf
#  which tunes TeX startup array sizes.
#  At least:
#     save_size = 30000       % for saving values outside current group
#  and:
#     % Max number of characters in all strings, including all error messages,
#     % help texts, font names, control sequences.  These values apply to TeX and MP.
#     pool_size.context = 750000
#     pool_size.jadetex = 750000
#     pool_size = 750000
#     % Minimum pool space after TeX/MP's own strings; must be at least
#     % 25000 less than pool_size, but doesn't need to be nearly that large.
#     string_vacancies.context = 55000
#     string_vacancies = 35000
#     % Maximum number of strings.
#     max_strings.context = 65000
#     max_strings.jadetex = 65000
#     max_strings = 35000
#

# We run LOCAL version of Jade Wrapper (jw), which needs PATH with leading dot
PATH := .:${PATH}

PARTS=	zmanual.sgml		zmanual-cover.sgml		\
	zadministration.sgml    zref-router.sgml		\
	zapp-filefmts.sgml      zref-scheduler.sgml		\
	zapp-listmgrs.sgml      zref-sendmail.sgml		\
	zapp-scripts.sgml       zref-smtpserver.sgml		\
	zapp-ssl-lang.sgml      zref-transport-agents.sgml	\
	zapp-tragents.sgml      zref-utilities.sgml		\
	zinstall.sgml           zref-zmailer3.sgml		\
	zreference.sgml		zref-rmail.sgml			\
	ztutorial.sgml		zappendices.sgml		\
	zadm-dnsissues.sgml	zadm-router.sgml		\
	zadm-transport-agents.sgml zadm-logging.sgml		\
	zadm-scheduler.sgml	zadm-smtpserver.sgml		\
	zadm-queues.sgml	zadm-security.sgml		\
	zapp-rfc821.sgml	zapp-rfc821.sgml		\
	zmanual.dsl

FIGS   :=	msgmodl.fig msgrout.fig zmailer-logo.fig \
		zmdirs.fig zmprocs.fig zmprocs-smtps.fig \
		zmprocs-router.fig zmprocs-scheduler.fig \
		zmprocs-tragents.fig zadm-sch-threads.fig \
		zmsched1.fig smtpserver-processes.fig

.SUFFIXES: .eps .pdf .ps .gif .fig .gif-green .png

FIGEPS :=	$(FIGS:%.fig=%.eps)
FIGPDF :=	$(FIGS:%.fig=%.pdf)
FIG_PS :=	$(FIGS:%.fig=%.ps)
FIGGIF :=	$(FIGS:%.fig=%.gif)

# set and export TEXINPUTS
TEXINPUTS=$(LATEX2HTMLLIB):$(LINUXDOCLIB):$$TEXINPUTS:
TEXENV=  TEXINPUTS=$(TEXINPUTS);export TEXINPUTS;

GIFS = $(L2HGIFS) zmailer-logo2.gif

all:
	@echo "make tag choices: clean dist html dvi ps pdf"

dvi:	zmanual.dvi
ps:	zmanual.ps
pdf:	zmanual.pdf
html:	zmanual.html
dist:	manual_parts.tar.gz

zmailer-logo2.gif zlogo: zmailer-logo2.gif-green
	cp -p zmailer-logo2.gif-green zmailer-logo2.gif

zmailer-logo2.gif-green: zmailer-logo.ps

gifs: $(FIGGIF)

$(L2HGIFS):

z-version.txt: ${PARTS} ${FIGS}
	echo "${VERSION}.`TZ=UTC date +%Y%m%d%H%M`" > z-version.txt


OJCAT = /usr/share/sgml/openjade-1.3.2/catalog

publish: zmanual.pdf zmanual.html 
	rm -rf /home/httpd/zmailer/html/zman/*
	cp -a zmanual/. /home/httpd/zmailer/html/zman/
	cp zmanual.pdf  /home/httpd/zmailer/html/zman/

zmanual.html: zmanual/zmanual.shtml

zmanual/zmanual.shtml: gifs $(PARTS)
	rm -rf zmanual
	TZ=UTC date > z-datetime.txt
	echo "HTML" > z-what.txt
	echo '<!ENTITY GEXT "gif">' > z-gext.txt
	make z-version.txt
	make index-html
	./db2html -c ${OJCAT} -d `/bin/pwd`/'zmanual.dsl#html' zmanual.sgml
	cp -p *.gif zmanual/
	cp zmanual.css2 zmanual/zmanual.css
	cd zmanual; ../html-post-fixup.sh *.html

HTML.index:
	touch genindex.sgm
	./db2html -c ${OJCAT} -d `/bin/pwd`/'zmanual.dsl#html' -V html-index zmanual.sgml
	mv zmanual/HTML.index .

genindex.sgm index-html index-dvi index-pdf: HTML.index
	collateindex.pl -d zman-index.html -f -g -i document-index -t Index -o genindex.sgm HTML.index

zmanual.ps: zmanual.dvi $(FIGEPS)
	dvips -ta4 -ozmanual.ps zmanual.dvi

zmanual.dvi: $(PARTS) $(FIGEPS) zmanual.sgml
	TZ=UTC date > z-datetime.txt
	echo "PostScript" > z-what.txt
	echo '<!ENTITY GEXT "eps">' > z-gext.txt
	make z-version.txt
	make index-dvi
	db2dvi -c ${OJCAT} -d `/bin/pwd`/'zmanual.dsl#print' zmanual.sgml

zmanual.pdf: $(PARTS) $(FIGPDF) zmanual.sgml
	TZ=UTC date > z-datetime.txt
	echo "PDF" > z-what.txt
	echo '<!ENTITY GEXT "pdf">' > z-gext.txt
	make z-version.txt
	make index-pdf
	db2pdf -c ${OJCAT} -d `/bin/pwd`/'zmanual.dsl#pdf'  zmanual.sgml


distclean clean:
	# Leave "zmanual.ps" in place, if it exists
	rm -f *~ *.html *.eps *.gif *.bak *.dvi *.toc *.lof *.pdf *.ps
	rm -f *.css *.aux *.ptr *.old *.ilg *.gls *.cb *.glo *.log
	rm -rf l2h[1-9][0-9]* *sections.pl *internals.pl #*index.pl
	rm -f *contents.pl *.ind *.idx *images.pl *table.pl
	rm -f [A-Z]*images.* *labels.pl *figure.pl IMG_PAR*
	rm -f TEXENV *.ps.gz *.xbm *.png
	rm -f *.gif-green z-datetime.txt
	rm -rf zmanual



%.gif: %.ps
	rm -f $@.ppm
	ghostscript -dNOPAUSE -sPAPERSIZE=a3 -sDEVICE=ppm -sOutputFile=$@.ppm $^ -c quit
	if [ ! -f $@.ppm ] ; then exit 1 ;fi # No file, abort!
	pnmcrop < $@.ppm | ppmtogif -transparent white > $@
	rm -f $@.ppm

%.gif-green: %.ps
	rm -f $@.ppm
	ghostscript -dNOPAUSE -sPAPERSIZE=a3 -sDEVICE=ppm -sOutputFile=$@.ppm $^ -c quit
	if [ ! -f $@.ppm ] ; then exit 1 ;fi # No file, abort!
	pnmcrop < $@.ppm | ppmtogif -transparent green > $@
	rm -f $@.ppm

%.ps: %.fig
	fig2dev -Lps -c -p x -z A3 -m 1.0000 -x 0 -y 0 -n $@ $< $@

zmailer-logo.eps: zmailer-logo.fig
	fig2dev -Leps -c -p x -z A3 -m 1.0000 -x 0 -y 0 -n $@ $< $@

%.eps: %.fig
	fig2dev -Leps -c -p x -z A3 -m 0.5000 -x 0 -y 0 -n $@ $< $@

%.pdf: %.eps
	: epstopdf $<
	sh ./eps2epdf $< $@


$(FIG_PS): $(FIGS)
$(FIGEPS): $(FIGS)
$(FIGPDF): $(FIGS)

manual_parts.tar.gz: manual_parts.tar

manual_parts.tar: Makefile $(PARTS) $(FIGS)
	tar -cf manual_parts.tar Makefile $(PARTS) $(FIGS)
	gzip -c manual_parts.tar > manual_parts.tar.gz


index:	html ps pdf
	gzip -9 < zmanual.ps > zmanual/zmanual.ps.gz
	cd zmanual ; ../makeindex.sh > index.html
