.PHONY: all clean

# Set to any non-empty value to enable compiling EMA version.
# EMA expected to be located in naa-comminication-prototype/ema/.
ENABLE_EMA :=

OPTIONS :=
CC      := gcc
DISABLEWARNINGS :=
CPPFLAGS := -D_POSIX_C_SOURCE=200112L -D_DEFAULT_SOURCE -Wall -std=c17 -pedantic -g -Wextra $(DISABLEWARNINGS)
CFLAGS  := -fstack-protector-all ${OPTIONS}
LD      := gcc
INCS_DEFAULT := -I${PWD}/lib/
LDS_DEFAULT := ${LDLIBS} -lrdmacm -libverbs
INCS_EMA := -I${PWD}/ema/include/
LDS_EMA  := -L${PWD}/ema/lib/ -lEMA
#-lpthread
.PRECIOUS: %.o

APPS_DEFAULT := naaice_client naaice_server
APPS_EMA := naaice_client_ema

all: check-ema apps

check-ema:
ifdef ENABLE_EMA
APPS = $(APPS_DEFAULT) $(APPS_EMA)
INCS = $(INCS_DEFAULT) $(INCS_EMA)
LDS = $(LDS_DEFAULT) $(LDS_EMA)
else
APPS = $(APPS_DEFAULT)
INCS = $(INCS_DEFAULT)
LDS = $(LDS_DEFAULT)
endif

apps: $(addprefix bin/,$(APPS))

bin/%: src/%.o lib/naaice.o lib/naaice_ap2.o lib/naaice_swnaa.o
	mkdir -p bin
	${LD} $(CFLAGS) -o $@ $^ ${LDS}

%.o: %.c lib/naaice.h lib/naaice_ap2.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCS) -c -o $@ $< -I${PWD}/lib

clean:
	rm -f lib/*.o src/*.o bin/*
