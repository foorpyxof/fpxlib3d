.PHONY: all prep release debug libs test clean shaders

include make/*.mk

CC = clang
FILE_EXT = .out

CFLAGS = -std=c99
LDFLAGS = -lglfw -lvulkan

RELEASE_FLAGS = -O3
DEBUG_FLAGS = -DDEBUG -g -Og

BUILD_FOLDER = build
SOURCE_FOLDER = src
INCLUDE_DIRS = include modules

CFLAGS += $(foreach dir,$(INCLUDE_DIRS),-I./$(dir))

RELEASE_APP = $(BUILD_FOLDER)/release$(FILE_EXT)
DEBUG_APP = $(BUILD_FOLDER)/debug$(FILE_EXT)

LIB_OBJECTS = vk

all: libs shaders

clean:
	rm -rf ./$(BUILD_FOLDER) || true

shaders: $(SHADER_FILES)

release: $(OBJECTS_RELEASE)
debug: $(OBJECTS_DEBUG)

# testing app
test: $(RELEASE_APP) $(DEBUG_APP)

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

$(RELEASE_APP): $(OBJECTS_RELEASE)
	$(CC) $(CFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) $(RELEASE_FLAGS) \
		$(SOURCE_FOLDER)/main.c \
		$< \
		-o $@

$(DEBUG_APP): $(OBJECTS_DEBUG)
	$(CC) $(CFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) $(DEBUG_FLAGS) \
		$(SOURCE_FOLDER)/main.c \
		$< \
		-o $@

$(SHADER_FILES): %.spv: %
	glslc $< -o $@
