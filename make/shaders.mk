SHADER_DIR = shaders
SHADER_SRC = default
SHADER_EXTENSIONS = vert frag
SHADER_PIPELINES = $(foreach source,$(SHADER_SRC),$(SHADER_DIR)/$(source))
SHADER_FILES = $(foreach ext,$(SHADER_EXTENSIONS),$(foreach pipeline,$(SHADER_PIPELINES),$(pipeline).$(ext).spv))
