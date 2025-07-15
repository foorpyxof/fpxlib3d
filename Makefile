.PHONY: all prep release debug libs test clean shaders

include make/*.mk

CC = clang
FILE_EXT = .out

CFLAGS = -std=c99
LDFLAGS = -lglfw -lvulkan

RELEASE_FLAGS = -O1
DEBUG_FLAGS = -DDEBUG -g -O0

BUILD_FOLDER = build
SOURCE_FOLDER = src
INCLUDE_DIRS = include modules

CFLAGS += $(foreach dir,$(INCLUDE_DIRS),-I./$(dir))

DEBUG_APP = $(BUILD_FOLDER)/debug$(FILE_EXT)
LIB_OBJECTS = vk

all: libs

prep:
	mkdir -p build/objects

release: $(OBJECTS_RELEASE)
debug: $(OBJECTS_DEBUG)
test: $(DEBUG_APP)

libs: $(OBJECTS_RELEASE) $(OBJECTS_DEBUG)

$(OBJECTS_RELEASE): prep
$(OBJECTS_RELEASE): $(OBJECTS_FOLDER)/%.o: $(SOURCE_FOLDER)/%.c
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) \
		-c $< -o $@

$(OBJECTS_DEBUG): prep
$(OBJECTS_DEBUG): $(OBJECTS_FOLDER)/%_debug.o: $(SOURCE_FOLDER)/%.c
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) \
		-c $< -o $@

$(DEBUG_APP): $(OBJECTS_DEBUG)
	$(CC) $(CFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) \
		$(SOURCE_FOLDER)/main.c \
		$(BUILD_FOLDER)/objects/*_debug.o \
		-o $@

clean:
	rm -rf ./$(BUILD_FOLDER) || true

shaders: $(SHADER_FILES)

$(SHADER_FILES):
	glslc $(basename $@) -o $@
