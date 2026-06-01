PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

VERSION_TAG := $(shell git describe --abbrev=6 --dirty --always --tags 2>/dev/null || echo unknown)

CFLAGS := -O3 -flto=thin -DNDEBUG -ffunction-sections -fdata-sections -Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes -Werror=strict-prototypes -Werror=missing-prototypes -D_BSD_SOURCE -std=gnu11 -Iinclude -Isrc
CFLAGS += -DMICROMOUNT_VERSION=\"$(VERSION_TAG)\"
LDFLAGS := -flto=thin -Wl,--gc-sections
LIBS :=

SRCS := src/main.c
OBJS := $(SRCS:.c=.o)
HEADERS := $(wildcard include/*.h)
DIST_DIR := dist
TARGET := $(DIST_DIR)/micromount.elf

all: $(TARGET)

$(DIST_DIR):
	mkdir -p $(DIST_DIR)

$(TARGET): $(OBJS) | $(DIST_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	$(PS5_PAYLOAD_SDK)/bin/prospero-strip --strip-all $@

src/%.o: src/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) src/*.o
