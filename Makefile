.PHONY: all clean

OPTIONS :=
CC      := gcc
DISABLEWARNINGS := 
CPPFLAGS := -D_POSIX_C_SOURCE=200112L -D_DEFAULT_SOURCE -Wall -std=c17 -pedantic -g -Wextra $(DISABLEWARNINGS)
CFLAGS  := -Ilib/ -fstack-protector-all ${OPTIONS}
LD      := gcc
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs
#-lpthread
.PRECIOUS: %.o


APPS    := naaice_client naaice_server
all: $(addprefix bin/,$(APPS))

bin/%: src/%.o lib/naaice.o lib/naaice_ap2.o lib/naaice_swnaa.o
	mkdir -p bin
	${LD} $(CFLAGS)    -o $@ $^ ${LDLIBS}

%.o: %.c lib/naaice.h lib/naaice_ap2.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $< -I${PWD}/lib

clean:
	rm -f lib/*.o src/*.o bin/*

