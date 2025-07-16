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

all: libs shaders

clean:
	rm -rf ./$(BUILD_FOLDER) || true

shaders: $(SHADER_FILES)

release: $(OBJECTS_RELEASE)
debug: $(OBJECTS_DEBUG)

# testing app
test: $(DEBUG_APP)

# individual libraries, both RELEASE and DEBUG
libs: $(OBJECTS_RELEASE) $(OBJECTS_DEBUG)

$(OBJECTS_FOLDER):
	mkdir -p $@

$(OBJECTS_RELEASE): $(OBJECTS_FOLDER)/%.o: $(SOURCE_FOLDER)/%.c | $(OBJECTS_FOLDER)
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) \
		-c $< -o $@

$(OBJECTS_DEBUG): $(OBJECTS_FOLDER)/%_debug.o: $(SOURCE_FOLDER)/%.c | $(OBJECTS_FOLDER)
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) \
		-c $< -o $@

$(DEBUG_APP): $(OBJECTS_DEBUG)
	$(CC) $(CFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) \
		$(SOURCE_FOLDER)/main.c \
		$(OBJECTS_DEBUG) \
		-o $@

$(SHADER_FILES): %.spv: %
	glslc $< -o $@
