CXX ?= c++
NODE_INCLUDE ?= $(shell node -p "require('path').resolve(process.execPath,'../../include/node')")
WASI_SDK ?= $(CURDIR)/.toolchains/wasi-sdk
CXXFLAGS ?= -O2 -Wall -Wextra -Wpedantic
NATIVE_FLAGS := -std=c++20 -ffp-contract=off -fno-fast-math -Iinclude -Isdk
ifeq ($(shell uname -s),Darwin)
ADDON_FLAGS := -bundle -undefined dynamic_lookup
else
ADDON_FLAGS := -shared -fPIC
endif

CORE := src/world.cpp src/geometry.cpp src/weapons.cpp src/movement.cpp src/skills.cpp
ADDON := $(CORE) src/addon.cpp src/replay_delta.cpp
HEADERS := src/world.hpp sdk/naval.hpp include/naval/geometry.hpp

.PHONY: all
all: build/naval.node player.wasm

build/naval.node: $(ADDON) $(HEADERS) Makefile
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(NATIVE_FLAGS) -I"$(NODE_INCLUDE)" $(ADDON_FLAGS) $(ADDON) -o "$@"

player.wasm: main.cpp sdk/naval.hpp Makefile
	"$(WASI_SDK)/bin/clang++" -std=c++20 -O2 -ffp-contract=off -Wall -Wextra -Isdk -nostartfiles \
	    -Wl,--no-entry -Wl,--export-memory -Wl,--max-memory=16777216 main.cpp -o "$@"
