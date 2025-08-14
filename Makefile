.PHONY: all prep release debug libs test clean shaders

# NOTE: remember to `make clean` after switching target OS
# WINDOWS := true

include make/*.mk

CC := clang
CC_WIN32 := x86_64-w64-mingw32-gcc

CFLAGS := -std=gnu11 -Wall -Wextra -Wpedantic -Werror -Wno-gnu-zero-variadic-macro-arguments -Wno-unknown-warning-option -Wno-variadic-macro-arguments-omitted
LDFLAGS := -lm

ifeq ($(WINDOWS), true)
	TARGET := win32

	-include make/windows/*.mk

	CC := $(CC_WIN32)
	CFLAGS += -mwindows -DVK_USE_PLATFORM_WIN32_KHR
	LDFLAGS += -lglfw3 -lvulkan-1
	# LDFLAGS += -lwinpthread-1
	
	EXE_EXT := .exe
else
	LDFLAGS += -lglfw -lvulkan
	
	EXE_EXT := .out
endif


RELEASE_FLAGS := -O3
DEBUG_FLAGS := -DDEBUG -g -O0
# DEBUG_FLAGS += -fsanitize=address

BUILD_FOLDER := build
SOURCE_FOLDER := src

# !!! ADD THESE VARIABLES ON YOUR OWN IF NECESSARY (FOR EXAMPLE WHEN CROSS COMPILING) !!!
EXTRA_INCLUDE_DIRS := $(VULKAN_INCLUDE_DIRECTORY) $(GLFW_INCLUDE_DIRECTORY) $(CGLM_INCLUDE_DIRECTORY)
EXTRA_LIB_DIRS := $(VULKAN_LIBRARY_DIRECTORY) $(GLFW_LIBRARY_DIRECTORY)

INCLUDE_DIRS := include modules $(EXTRA_INCLUDE_DIRS)
LIB_DIRS := $(EXTRA_LIB_DIRS)

CFLAGS += $(foreach dir,$(INCLUDE_DIRS),-I$(dir))
LDFLAGS += $(foreach dir,$(LIB_DIRS),-L$(dir))

RELEASE_APP := $(BUILD_FOLDER)/release$(EXE_EXT)
DEBUG_APP := $(BUILD_FOLDER)/debug$(EXE_EXT)

MAIN := $(SOURCE_FOLDER)/main.c

LIB_OBJECTS := vk

all: libs shaders

clean:
	rm -rf ./$(BUILD_FOLDER) || true

shaders: $(SHADER_FILES)

release: $(OBJECTS_RELEASE)
debug: $(OBJECTS_DEBUG)

# testing app
test: $(RELEASE_APP) $(DEBUG_APP) $(SHADER_FILES)

# individual libraries, both RELEASE and DEBUG
libs: $(OBJECTS_RELEASE) $(OBJECTS_DEBUG)

$(OBJECTS_FOLDER):
	mkdir -p $@

$(OBJECTS_RELEASE): $(OBJECTS_FOLDER)/%.o: $(SOURCE_FOLDER)/%.c Makefile | $(OBJECTS_FOLDER)
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) -c $< -o $@

$(OBJECTS_DEBUG): $(OBJECTS_FOLDER)/%_debug.o: $(SOURCE_FOLDER)/%.c Makefile | $(OBJECTS_FOLDER)
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) -c $< -o $@

$(RELEASE_APP): LDFLAGS += -s
$(RELEASE_APP): $(OBJECTS_RELEASE) $(MAIN)
	$(CC) $(CFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) $(RELEASE_FLAGS) $^ -o $@

$(DEBUG_APP): $(OBJECTS_DEBUG) $(MAIN)
	$(CC) $(CFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) $(DEBUG_FLAGS) $^ -o $@

$(SHADER_FILES): %.spv: %
	glslc $< -o $@
