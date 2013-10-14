
CFLAGS = -Wall -W $(shell pkg-config --cflags fuse libparted)
LDFLAGS = $(shell pkg-config --libs fuse libparted)

PREFIX ?= /usr/local

.PHONY: all

all: fldev fldev.1 README.html

fldev: fldev.o


fldev.1: README.txt
	a2x -f manpage README.txt
	rm -f README.xml

README.html: README.txt
	a2x -f xhtml -d manpage README.txt
	rm -f README.xml


dist: distclean

test: fldev
	./tests.sh

clean distclean:
	rm -f fldev fldev.o *~ README.xml

maintainer-clean: clean
	rm -f fldev.1 README.html README.xml


install: fldev fldev.1
	install -s -m 0755 fldev $(PREFIX)/bin/fldev
	install -d $(PREFIX)/man/man1
	install -m 0644 fldev.1 $(PREFIX)/man/man1/fldev.1

uninstall:
	rm -f $(PREFIX)/bin/fldev
	rm -f $(PREFIX)/man/man1/fldev.1
