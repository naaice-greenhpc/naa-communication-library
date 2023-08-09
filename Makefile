.PHONY: all clean
#MAX_TRANSFER_LENGTH := 1024
MAX_TRANSFER_LENGTH   := 1073741824ll
NUMBER_OF_REPITITIONS := 2
OPTIONS := -D MAX_TRANSFER_LENGTH=${MAX_TRANSFER_LENGTH} -D NUMBER_OF_REPITITIONS=${NUMBER_OF_REPITITIONS}

CC      := gcc
CFLAGS  := -Wall -pedantic -g -Wextra -Ilib/ -fstack-protector-all ${OPTIONS}
LD      := gcc
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs
#-lpthread

APPS    := naaice_client naaice_server
all: $(addprefix bin/,$(APPS))

bin/%: src/%.o lib/naaice.o
	mkdir -p bin
	${LD} $(CFLAGS)    -o $@ $^ ${LDLIBS}

%.o: %.c lib/naaice.h
	$(LD) $(CFLAGS) -c -o $@ $<

clean:
	rm -f lib/*.o src/*.o bin/*
