.PHONY: all clean

OPTIONS := -D
CC      := gcc
CFLAGS  := -Wall -pedantic -g -Wextra -Ilib/ -fstack-protector-all ${OPTIONS}
LD      := gcc
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs
#-lpthread

APPS    := naaice_client naaice_server
all: $(addprefix bin/,$(APPS))

bin/%: src/%.o lib/naaice.o lib/naaice_ap2.o lib/naaice_swnaa.o
	mkdir -p bin
	${LD} $(CFLAGS)    -o $@ $^ ${LDLIBS}

%.o: %.c lib/naaice.h lib/naaice_ap2.h
	$(LD) $(CFLAGS) -c -o $@ $<

clean:
	rm -f lib/*.o src/*.o bin/*