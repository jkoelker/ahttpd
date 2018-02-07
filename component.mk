#
#

ifdef CONFIG_AHTTPD_ENABLE

COMPONENT_ADD_INCLUDEDIRS := .
COMPONENT_SRCDIRS := . http-parser espfs

ifdef CONFIG_AHTTPD_ENABLE_ESPFS
CFLAGS += -DCONFIG_AHTTPD_ENABLE_ESPFS
COMPONENT_EXTRA_CLEAN := \
	libwebpages-espfs.a \
	webpages.espfs \
	webpages.espfs.o \
	webpages.espfs.o.tmp \
	mkespfsimage/heatshrink_encoder.o \
	mkespfsimage/main.o \
	mkespfsimage/mkespfsimage

libahttpd.a: libwebpages-espfs.a

ifdef CONFIG_AHTTPD_HTML_GENERATE_CMD
HTML_CMD := $(shell echo $(CONFIG_AHTTPD_HTML_GENERATE_CMD) \
	$(PROJECT_PATH)/$(CONFIG_AHTTPD_HTMLDIR))
COMPONENT_EXTRA_CLEAN += $(PROJECT_PATH)/$(CONFIG_AHTTPD_HTMLDIR)/*
$(PROJECT_PATH)/$(CONFIG_AHTTPD_HTMLDIR):
	cd $(PROJECT_PATH) && $(HTML_CMD)
endif  # CONFIG_AHTTPD_HTML_GENERATE_CMD


ifdef CONFIG_AHTTPD_ESPFS_GZIP
GZIP_FILES := $(shell echo "-g" $(CONFIG_AHTTPD_ESPFS_GZIP_EXTS))
GZIP_COMPRESSION := "yes"
else
GPIZ_FILES :=
GZIP_COMPRESSION := "no"
endif  # CONFIG_AHTTPD_ESPFS_GZIP


ifdef CONFIG_AHTTPD_ESPFS_HEATSHRINK
USE_HEATSHRINK := "yes"
CFLAGS += -DESPFS_HEATSHRINK
COMPONENT_SRCDIRS += espfs/heatshrink
else
USE_HEATSHRINK := "no"
endif  # CONFIG_AHTTPD_ESPFS_HEATSHRINK


webpages.espfs: $(PROJECT_PATH)/$(CONFIG_AHTTPD_HTMLDIR) mkespfsimage/mkespfsimage
	cd $(PROJECT_PATH)/$(CONFIG_AHTTPD_HTMLDIR) && \
		pwd && \
		find . | $(COMPONENT_BUILD_DIR)/mkespfsimage/mkespfsimage \
					$(GZIP_FILES) > $(COMPONENT_BUILD_DIR)/webpages.espfs

libwebpages-espfs.a: webpages.espfs
	$(OBJCOPY) -I binary -O elf32-xtensa-le -B xtensa --rename-section \
		.data=.rodata webpages.espfs webpages.espfs.o.tmp
	$(CC) -nostdlib -Wl,-r webpages.espfs.o.tmp -o webpages.espfs.o \
		-Wl,-T $(COMPONENT_PATH)/espfs/webpages.espfs.esp32.ld
	$(AR) cru $@ webpages.espfs.o

mkespfsimage/mkespfsimage: $(COMPONENT_PATH)/espfs/mkespfsimage
	mkdir -p $(COMPONENT_BUILD_DIR)/mkespfsimage
	$(MAKE) -C $(COMPONENT_BUILD_DIR)/mkespfsimage \
			-f $(COMPONENT_PATH)/espfs/mkespfsimage/Makefile \
				USE_HEATSHRINK="$(USE_HEATSHRINK)" \
				GZIP_COMPRESSION="$(GZIP_COMPRESSION)" \
				BUILD_DIR=$(COMPONENT_BUILD_DIR)/mkespfsimage \
				CC=$(HOSTCC)

endif  # CONFIG_AHTTPD_ENABLE_ESPFS

endif
