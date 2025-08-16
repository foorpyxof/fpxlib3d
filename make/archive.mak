ARCHIVES_DIR := archives

ARCHIVE_FILE_NAME := fpxlib3d
DONT_ARCHIVE_DIRS := .git .cache scripts $(ARCHIVES_DIR) $(MODULES_DIR)
DONT_ARCHIVE_FILES := $(RELEASE_APP) $(DEBUG_APP) compile_commands.json .gitattributes .gitignore .copywrite.hcl

ARCHIVE_FILE_NAME := $(ARCHIVE_FILE_NAME)-$(TARGET)

MODULES := $(wildcard $(MODULES_DIR)/*)
LICENSES = $(wildcard $(module)/LICENSE*)
LICENSES_DIR := third_party_licenses

ifeq ($(TARGET), $(WINDOWS_TARGET_NAME))
	ARCHIVE_COMMAND := zip -qr
	ARCHIVE_EXTENSION := zip

	EXCLUDE := -x $(foreach dirname,$(DONT_ARCHIVE_DIRS),"$(ROOT_DIR_NAME)/$(dirname)/*")
	EXCLUDE += $(foreach filename,$(DONT_ARCHIVE_FILES),"$(ROOT_DIR_NAME)/$(filename)") @
else
	COMPRESSION := gzip

ifeq ($(COMPRESSION),gzip)
	FLAG := z
	COMPRESSION_EXT := .gz
else ifeq ($(COMPRESSION),xz)
	FLAG := J
	COMPRESSION_EXT := .xz
else ifeq ($(COMPRESSION),bzip2)
	FLAG := j
	COMPRESSION_EXT := .bz2
endif

	ARCHIVE_COMMAND := tar -c$(FLAG)hf
	ARCHIVE_EXTENSION := tar$(COMPRESSION_EXT)

	DONT_ARCHIVE_FILES += *.dll

	EXCLUDE := $(foreach dirname,$(DONT_ARCHIVE_DIRS),--exclude=$(ROOT_DIR_NAME)/$(dirname))
	EXCLUDE += $(foreach filename,$(DONT_ARCHIVE_FILES),--exclude=$(ROOT_DIR_NAME)/$(filename))
endif

FULL_ARCHIVE_NAME := $(ARCHIVE_FILE_NAME).$(ARCHIVE_EXTENSION)

ARCHIVE_COMMAND += $(FULL_ARCHIVE_NAME) $(EXCLUDE) $(ROOT_DIR_NAME)
ARCHIVE_COMMAND += ;
ARCHIVE_COMMAND += mv $(FULL_ARCHIVE_NAME) $(ROOT_DIR)/$(ARCHIVES_DIR)

.PHONY: archive
archive:
	-mkdir $(LICENSES_DIR)
	-$(foreach module,$(MODULES),$(foreach license,$(LICENSES),ln -s $(ROOT_DIR)/$(license) $(ROOT_DIR)/$(LICENSES_DIR)/$(shell basename $(module))_$(shell basename $(license));))
	-mv $(RELEASE_APP) $(DEBUG_APP) .
	-mkdir $(ARCHIVES_DIR); cd ..; $(ARCHIVE_COMMAND)
	-mv $(shell basename $(RELEASE_APP)) $(shell basename $(DEBUG_APP)) $(BUILD_FOLDER)/
	-rm -rf $(LICENSES_DIR)
