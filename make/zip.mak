ZIPS_DIR := archives

ZIP_FILE_NAME := fpxlib3d
DONT_ZIP_DIRS := .git .cache scripts $(ZIPS_DIR) $(MODULES_DIR) $(OBJECTS_FOLDER)
DONT_ZIP_FILES := $(RELEASE_APP) $(DEBUG_APP) compile_commands.json .gitattributes .gitignore .copywrite.hcl

ZIP_FILE_NAME := $(ZIP_FILE_NAME)-$(TARGET)

MODULES := $(wildcard $(MODULES_DIR)/*)
LICENSES = $(wildcard $(module)/LICENSE*)
LICENSES_DIR := third_party_licenses

ifeq ($(TARGET),$(WINDOWS_TARGET_NAME))
	ZIP_COMMAND := zip -qr
	ZIP_EXTENSION := zip

	EXCLUDE := -x $(foreach dirname,$(DONT_ZIP_DIRS),"$(ROOT_DIR_NAME)/$(dirname)/*")
	EXCLUDE += $(foreach filename,$(DONT_ZIP_FILES),"$(ROOT_DIR_NAME)/$(filename)") @
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

	ZIP_COMMAND := tar -c$(FLAG)hf
	ZIP_EXTENSION := tar$(COMPRESSION_EXT)

	DONT_ZIP_FILES += *.dll

	EXCLUDE := $(foreach dirname,$(DONT_ZIP_DIRS),--exclude=$(ROOT_DIR_NAME)/$(dirname))
	EXCLUDE += $(foreach filename,$(DONT_ZIP_FILES),--exclude=$(ROOT_DIR_NAME)/$(filename))
endif

FULL_ZIP_NAME := $(ZIP_FILE_NAME).$(ZIP_EXTENSION)

ZIP_COMMAND += $(FULL_ZIP_NAME) $(EXCLUDE) $(ROOT_DIR_NAME)
ZIP_COMMAND += ;
ZIP_COMMAND += mv $(FULL_ZIP_NAME) $(ROOT_DIR)/$(ZIPS_DIR)

.PHONY: archive
archive:
	-mkdir $(LICENSES_DIR)
	-$(foreach module,$(MODULES),$(foreach license,$(LICENSES),ln -s $(ROOT_DIR)/$(license) $(ROOT_DIR)/$(LICENSES_DIR)/$(shell basename $(module))_$(shell basename $(license));))
	-mv $(RELEASE_APP) $(DEBUG_APP) .
	-mkdir $(ZIPS_DIR); cd ..; $(ZIP_COMMAND)
	-mv $(shell basename $(RELEASE_APP)) $(shell basename $(DEBUG_APP)) $(BUILD_FOLDER)/
	-rm -rf $(LICENSES_DIR)
