PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

VERSION_TAG := $(shell git describe --abbrev=6 --dirty --always --tags 2>/dev/null || echo unknown)

CFLAGS := -O3 -flto=thin -DNDEBUG -ffunction-sections -fdata-sections -Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes -Werror=strict-prototypes -Werror=missing-prototypes -D_BSD_SOURCE -std=gnu11 -Iinclude -Isrc
CFLAGS += -DMICROMOUNT_VERSION=\"$(VERSION_TAG)\"
LDFLAGS := -flto=thin -Wl,--gc-sections
LIBS := -lSceNotification -lSceUserService

SRCS := src/main.c \
	src/mm_config.c \
	src/mm_log.c \
	src/mm_mount.c \
	src/mm_scan.c \
	src/mm_sha256.c \
	src/mm_util.c
OBJS := $(SRCS:.c=.o)
HEADERS := $(wildcard include/*.h)
TARGET := micromount.elf

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	$(PS5_PAYLOAD_SDK)/bin/prospero-strip --strip-all $@

src/%.o: src/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) src/*.o
