TOPDIR := $(shell cd ../../;pwd)
include $(TOPDIR)/build/base.mk

TARGET := APP0
OBJS := $(patsubst %.c,%.o,$(wildcard src/*.c))
LDFLAGS += -lcommon -lm -lssl -lcurl -lcJSON -lcrypto -lpthread -lrt -ldl
CFLAGS += -I./inc -I../libam
CFLAGS += -D__AM_SERVICE_SUPPORT__ -DCONFIG_SUNMI_APP_PLATFORM_PRINTER_80

all: $(TARGET)
include $(MAKE_APP)
include $(MAKE_CLEAN)
