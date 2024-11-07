.PHONY: all clean

# Set to any non-empty value to enable compiling EMA version.
# EMA expected to be located in naa-comminication-prototype/ema/.
ENABLE_EMA :=

BUILD_DIR ?= ./bin/
SRC_DIR ?= ./src/
LIB_DIR ?= ./lib/
HDR_DIRS ?= $(shell find ./ -name include -type d)

INC_DIRS := $(shell find $(LIB_DIR) -type d)
INC_DIRS += $(HDR_DIRS)
INC := $(addprefix -I,$(INC_DIRS))

APPS_C := naaice_client naaice_server naaice_client_ap2
APPS_CPP := #naaice_client_ap2_cpp
APPS_EMA_C := naaice_client_ema
APPS_DEFAULT := $(APPS_C) $(APPS_CPP)

APP_SRCS := $(shell find $(SRC_DIR) -name *.cpp -o -name *.c)
LIB_SRCS := $(shell find $(LIB_DIR) -name *.cpp -o -name *.c)
TMPLTS := $(shell find $(LIB_DIRS) -name *.tpp)
OBJS := $(LIB_SRCS:%=%.o)

LD_DEFAULT := ${LDLIBS} -lrdmacm -libverbs
LD_EMA := -L${PWD}/ema/lib/ -lEMA

CPPFLAGS ?= $(INC) -MMD -MP -std=c++20 -Wall -pedantic -g -Wextra -gdwarf-2
CXXFLAGS ?= $(INC) -g

all: check-ema apps

check-ema:
ifdef ENABLE_EMA
APPS = $(APPS_DEFAULT) $(APPS_EMA_C)
LDFLAGS = $(LD_DEFAULT) $(LD_EMA)
else
APPS = $(APPS_DEFAULT)
LDFLAGS = $(LD_DEFAULT)
endif

release: CXXFLAGS += -DNDEBUG -g
release: apps

apps: $(addprefix $(BUILD_DIR), $(APPS))

# Executables
#bin/%: src/%.c.o $(OBJS) $(TMPLTS)
#	$(MKDIR_P) $(BUILD_DIR)
#	$(CXX) $(OBJS) $< -o $@ $(LDFLAGS)

# C Executables
$(APPS_C:%=$(BUILD_DIR)%): $(BUILD_DIR)%: src/%.c.o $(OBJS) $(TMPLTS)
	$(MKDIR_P) $(BUILD_DIR)
	$(CXX) $(OBJS) $< -o $@ $(LDFLAGS)

# C++ Executables
$(APPS_CPP:%=$(BUILD_DIR)%): $(BUILD_DIR)%: src/%.cpp.o $(OBJS) $(TMPLTS)
	$(MKDIR_P) $(BUILD_DIR)
	$(CXX) $(OBJS) $< -o $@ $(LDFLAGS)

# C source
%.c.o: %.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

# C++ source
%.cpp.o: %.cpp
	$(CXX) $(CPPFLAGS) -c $< -o $@

clean:
	$(RM) -f lib/*.o src/*.o lib/*.d src/*.d bin/*

MKDIR_P ?= mkdir -p
RM ?= rm