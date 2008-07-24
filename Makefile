CC=gcc
CFLAGS=-O2 -Wall -DUNICODE -D_UNICODE
LDFLAGS=-s

all: hardlink.exe

hardlink.exe: hardlink.c md5.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o hardlink.exe hardlink.c md5.c

clean:

distclean: clean
	rm -f hardlink.exe
