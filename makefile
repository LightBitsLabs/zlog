# Top level makefile, the real shit is at src/makefile

TARGETS=noopt 32bit
REVISION=$(shell git rev-parse --short=12 HEAD)
all:
	cd src && $(MAKE) $@
	mkdir -p build
	export PREFIX=`pwd`/build && cd src && $(MAKE) install

install:
	cd src && $(MAKE) $@

$(TARGETS):
	cd src && $(MAKE) $@

doc:
	cd doc && $(MAKE)

test:
	cd test && $(MAKE)

TAGS:
	find . -type f -name "*.[ch]" | xargs etags -

clean:
	cd src && $(MAKE) $@
	cd test && $(MAKE) $@
	cd doc && $(MAKE) $@
	rm -f TAGS

checkin:
	osmosis checkin $(PWD)/build $(REVISION)__zlog --objectStores=osmosis.lbits:1010
	
distclean: clean

dummy:

.PHONY: doc install test TAGS
