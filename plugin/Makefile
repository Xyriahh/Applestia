# HyprGlass Plugin

CXX ?= g++
CC ?= gcc
WAYLAND_SCANNER ?= wayland-scanner
CXXFLAGS = -fPIC -g -O2 -std=c++23
# Never ask for an executable stack, whatever an object or the linker defaults to:
# glibc 2.41+ refuses to load such a library.
LDFLAGS = -shared -Wl,-z,noexecstack
INCLUDES = $(shell pkg-config --cflags hyprland pixman-1 libdrm)
LIBS = $(shell pkg-config --libs hyprland) -ldl

# Shown by `hyprctl plugin list`. Release builds pass HYPRGLASS_VERSION; other
# builds describe their own checkout (never an enclosing repository), keeping
# only characters safe in a C string. Changing it needs `make clean`.
ifndef HYPRGLASS_VERSION
HYPRGLASS_VERSION := $(shell [ -e .git ] && git describe --tags --dirty 2>/dev/null | sed 's/^v//' | tr -cd 'A-Za-z0-9._+-')
endif
ifeq ($(HYPRGLASS_VERSION),)
HYPRGLASS_VERSION := dev
endif
CXXFLAGS += -DHYPRGLASS_VERSION='"$(HYPRGLASS_VERSION)"'

ifeq ($(basename $(CXX)),g++)
	CXXFLAGS += --no-gnu-unique
endif

TARGET = hyprglass.so
SOURCES = src/main.cpp src/GlassDecoration.cpp src/GlassPassElement.cpp src/GlassRenderer.cpp src/GlassLayerSurface.cpp src/GlassLayerPassElement.cpp src/GlassLayerCompositeElement.cpp src/BackgroundDamageObserver.cpp src/PluginConfig.cpp src/ShaderManager.cpp src/Diagnostics.cpp src/GlassSubsurfaceState.cpp src/GlassSubsurfacePassElement.cpp src/GlassSubsurfaceCompositeElement.cpp src/ItemHints.cpp src/GlassShapes.cpp src/GlassShapesRenderer.cpp src/SilhouetteField.cpp
OBJ = $(SOURCES:.cpp=.o)
HEADERS = $(wildcard src/*.hpp)

# ── hyprglass_item_v1 protocol helper ────────────────────────────────────────
# Built as its own shared library and embedded (via ItemHelperBlob.S) inside
# hyprglass.so, so it can be dlopen'd with RTLD_NODELETE and outlive a plugin
# reload — see AGENTS.md's unload-safety note. Depends only on
# libwayland-server, never on Hyprland headers.

PROTOCOL_XML    = protocols/hyprglass-item-v1.xml
ITEM_HELPER_DIR = src/item-helper
PROTOCOL_HEADER = $(ITEM_HELPER_DIR)/hyprglass-item-v1-server-protocol.h
PROTOCOL_CODE   = $(ITEM_HELPER_DIR)/hyprglass-item-v1-protocol.c

HELPER_SO       = $(ITEM_HELPER_DIR)/hyprglass-item-helper.so
HELPER_SOURCES  = $(ITEM_HELPER_DIR)/helper.c $(PROTOCOL_CODE)
HELPER_OBJ      = $(HELPER_SOURCES:.c=.o)

WAYLAND_SERVER_CFLAGS = $(shell pkg-config --cflags wayland-server)
WAYLAND_SERVER_LIBS   = $(shell pkg-config --libs wayland-server)

ITEM_HELPER_BLOB_OBJ = src/ItemHelperBlob.o

# Independent persistent helper for compositor-native per-control shapes.
SHAPES_XML = protocols/applestia-glass-shapes-v1.xml
SHAPES_HELPER_DIR = src/shapes-helper
SHAPES_GENERATED_DIR = build/shapes-helper
SHAPES_HEADER = $(SHAPES_GENERATED_DIR)/applestia-glass-shapes-v1-server-protocol.h
SHAPES_CODE = $(SHAPES_GENERATED_DIR)/applestia-glass-shapes-v1-protocol.c
SHAPES_SO = $(SHAPES_HELPER_DIR)/applestia-shapes-helper.so
SHAPES_OBJ = $(SHAPES_HELPER_DIR)/helper.o $(SHAPES_CODE:.c=.o)
SHAPES_BLOB_OBJ = src/ShapesHelperBlob.o

all: $(TARGET)

%.o : %.cpp $(HEADERS)
	@echo "[$(CXX)] $<"
	@$(CXX) -c $(CXXFLAGS) $(INCLUDES) $< -o $@

$(PROTOCOL_HEADER): $(PROTOCOL_XML)
	@echo "[wayland-scanner] server-header $<"
	@$(WAYLAND_SCANNER) server-header $< $@

$(PROTOCOL_CODE): $(PROTOCOL_XML)
	@echo "[wayland-scanner] private-code $<"
	@$(WAYLAND_SCANNER) private-code $< $@

# helper.c includes the generated server header; every helper object depends
# on it existing first, not just the .c file the pattern rule already covers.
$(HELPER_OBJ): $(PROTOCOL_HEADER)

$(ITEM_HELPER_DIR)/%.o: $(ITEM_HELPER_DIR)/%.c
	@echo "[$(CC)] $<"
	@$(CC) -c -fPIC -O2 -std=c11 $(WAYLAND_SERVER_CFLAGS) -I$(ITEM_HELPER_DIR) $< -o $@

$(HELPER_SO): $(HELPER_OBJ)
	@echo "Linking $(HELPER_SO)..."
	@$(CC) -shared -fPIC -Wl,-z,noexecstack -o $@ $(HELPER_OBJ) $(WAYLAND_SERVER_LIBS)

# .incbin's path is relative to this Makefile's own working directory (the
# repo root), the same base every other SOURCES path here is written against.
$(ITEM_HELPER_BLOB_OBJ): src/ItemHelperBlob.S $(HELPER_SO)
	@echo "[as] $<"
	@$(CC) -c $< -o $@

$(SHAPES_HEADER): $(SHAPES_XML)
	@mkdir -p $(SHAPES_GENERATED_DIR)
	@$(WAYLAND_SCANNER) server-header $< $@

$(SHAPES_CODE): $(SHAPES_XML)
	@mkdir -p $(SHAPES_GENERATED_DIR)
	@$(WAYLAND_SCANNER) private-code $< $@

$(SHAPES_OBJ): $(SHAPES_HEADER) $(SHAPES_HELPER_DIR)/applestia_shapes_helper_api.h

$(SHAPES_OBJ): %.o: %.c
	@echo "[$(CC)] $<"
	@$(CC) -c -fPIC -O2 -std=c11 $(WAYLAND_SERVER_CFLAGS) -I$(SHAPES_HELPER_DIR) -I$(SHAPES_GENERATED_DIR) $< -o $@

$(SHAPES_SO): $(SHAPES_OBJ)
	@echo "Linking $(SHAPES_SO)..."
	@$(CC) -shared -fPIC -Wl,-z,noexecstack -o $@ $(SHAPES_OBJ) $(WAYLAND_SERVER_LIBS)

$(SHAPES_BLOB_OBJ): src/ShapesHelperBlob.S $(SHAPES_SO)
	@echo "[as] $<"
	@$(CC) -c $< -o $@

src/GlassShapes.o: $(SHAPES_HELPER_DIR)/applestia_shapes_helper_api.h $(SHAPES_HELPER_DIR)/CommitSnapshots.hpp $(SHAPES_HELPER_DIR)/applestia_shapes_loader_path.h

build/shapes-helper-test: $(SHAPES_HELPER_DIR)/test.c $(SHAPES_HELPER_DIR)/helper.c $(SHAPES_HELPER_DIR)/applestia_shapes_helper_api.h $(SHAPES_HEADER) $(SHAPES_CODE)
	@mkdir -p build
	@$(CC) -std=c11 -O2 -Wall -Wextra -Werror $(WAYLAND_SERVER_CFLAGS) -I$(SHAPES_HELPER_DIR) -I$(SHAPES_GENERATED_DIR) $(SHAPES_HELPER_DIR)/test.c $(SHAPES_CODE) -o $@ $(WAYLAND_SERVER_LIBS) -lm

build/shapes-commits-test: $(SHAPES_HELPER_DIR)/test-commits.cpp $(SHAPES_HELPER_DIR)/CommitSnapshots.hpp $(SHAPES_HELPER_DIR)/applestia_shapes_helper_api.h
	@mkdir -p build
	@$(CXX) -std=c++23 -O2 -Wall -Wextra -Werror $(SHAPES_HELPER_DIR)/test-commits.cpp -o $@ $(shell pkg-config --cflags --libs hyprutils)

build/shapes-loader-test: $(SHAPES_HELPER_DIR)/loader-test.c $(SHAPES_HELPER_DIR)/applestia_shapes_loader_path.h $(SHAPES_HELPER_DIR)/applestia_shapes_helper_api.h $(HELPER_SO) $(SHAPES_SO)
	@mkdir -p build
	@$(CC) -D_GNU_SOURCE -std=c11 -O2 -Wall -Wextra -Werror $(SHAPES_HELPER_DIR)/loader-test.c -o $@ -ldl

test-shapes-helper: build/shapes-helper-test build/shapes-commits-test build/shapes-loader-test
	@./build/shapes-helper-test
	@./build/shapes-commits-test
	@./build/shapes-loader-test

$(TARGET): $(OBJ) $(ITEM_HELPER_BLOB_OBJ) $(SHAPES_BLOB_OBJ)
	@echo "Linking $(TARGET)..."
	@$(CXX) $(LDFLAGS) $(OBJ) $(ITEM_HELPER_BLOB_OBJ) $(SHAPES_BLOB_OBJ) -o $@ $(LIBS)
	@echo "Done!"

clean:
	rm -f $(OBJ) $(TARGET) $(ITEM_HELPER_BLOB_OBJ) $(HELPER_OBJ) $(HELPER_SO) $(PROTOCOL_HEADER) $(PROTOCOL_CODE) $(SHAPES_OBJ) $(SHAPES_SO) $(SHAPES_HEADER) $(SHAPES_CODE) $(SHAPES_BLOB_OBJ)

.PHONY: all clean test-shapes-helper
