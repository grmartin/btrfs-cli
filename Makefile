# btrfs-cli: builds the btrfs-fuse core (unmodified, out-of-tree) plus our CLI.
#
# Dependencies (Homebrew): libb2 openssl@3 xxhash zstd lzo pkg-config
# zlib and libuuid come from macOS itself.

BTRFS_FUSE ?= btrfs-fuse
BUILD      ?= build
PREFIX     ?= /usr/local

PKGS := libb2 libcrypto libxxhash libzstd zlib
# Homebrew's openssl@3 is keg-only, so add its pkgconfig dir if present.
OPENSSL_PC := $(shell brew --prefix openssl@3 2>/dev/null)/lib/pkgconfig
export PKG_CONFIG_PATH := $(OPENSSL_PC):$(PKG_CONFIG_PATH)

PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS))
PKG_LIBS   := $(shell pkg-config --libs $(PKGS))

# LZO is optional, as in btrfs-fuse's meson.build.
LZO_PREFIX := $(shell brew --prefix lzo 2>/dev/null)
ifneq ($(wildcard $(LZO_PREFIX)/include/lzo/lzo1x.h),)
LZO_CFLAGS := -DHAVE_LZO -I$(LZO_PREFIX)/include
LZO_LIBS   := -L$(LZO_PREFIX)/lib -llzo2
endif

CC      ?= cc
CFLAGS  ?= -O2 -g
WARN    := -Wall -Wno-unused-function -Wno-gnu-variable-sized-type-not-at-end \
           -Wno-address-of-packed-member
# compat/include must come first so its shims shadow the Linux-only headers.
CPPFLAGS += -Icompat/include -include compat/prelude.h -I$(BTRFS_FUSE) \
            $(PKG_CFLAGS) $(LZO_CFLAGS)
LDLIBS  += $(PKG_LIBS) $(LZO_LIBS)

# Everything except main.c (FUSE glue) and messages.c (replaced by
# src/messages.c).
CORE_SRC := accessors.c hash.c metadata.c super.c volumes.c \
            inode.c data.c compression.c libs/crc32c.c libs/rbtree.c \
            libs/raid56.c libs/tables.c
CLI_SRC  := src/main.c src/compat.c src/messages.c

CORE_OBJ := $(addprefix $(BUILD)/core/,$(CORE_SRC:.c=.o))
CLI_OBJ  := $(addprefix $(BUILD)/,$(CLI_SRC:.c=.o))

all: $(BUILD)/btrfs-cli

$(BUILD)/btrfs-cli: $(CORE_OBJ) $(CLI_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/core/%.o: $(BTRFS_FUSE)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -MMD -MP -c -o $@ $<

$(BUILD)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -MMD -MP -c -o $@ $<

install: $(BUILD)/btrfs-cli
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $< $(DESTDIR)$(PREFIX)/bin/btrfs-cli

clean:
	rm -rf $(BUILD)

-include $(CORE_OBJ:.o=.d) $(CLI_OBJ:.o=.d)

.PHONY: all install clean
