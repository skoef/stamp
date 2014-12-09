CC=cc
CFLAGS=-std=c99 -Wall -Werror
PREFIX=/usr/local
LDFLAGS=
BATS=$$(which bats)

ifeq ($(OS),Windows_NT)
LDFLAGS=-lpcre
endif

all: stamp

stamp: stamp.o
	$(CC) $(CFLAGS) stamp.o -o stamp $(LDFLAGS)

stamp.o: stamp.c
	$(CC) $(CFLAGS) -c stamp.c

clean:
	rm stamp
	rm *.o

install: all
	if [ ! -d $(PREFIX)/man/man1 ];then	\
		mkdir -p $(PREFIX)/man/man1;	\
	fi
	cp stamp.1 $(PREFIX)/man/man1/
	gzip $(PREFIX)/man/man1/stamp.1
	cp stamp $(PREFIX)/bin/

uninstall:
	rm $(PREFIX)/bin/stamp
	rm $(PREFIX)/man/man1/stamp.1.gz

test:
ifeq ($(BATS),)
	@echo "Error: BATS not found"
	@echo "install it from https://github.com/sstephenson/bats"
	@exit 1
endif
	@$$(which bats) tests.sh
