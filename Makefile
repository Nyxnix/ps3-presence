PS3DEV ?= /opt/ps3dev
PSL1GHT ?= $(PS3DEV)
export PS3DEV PSL1GHT
CC_PPU := $(PS3DEV)/ppu/bin/ppu-gcc
PYTHON ?= python3
DEPFLAGS := -MMD -MP
DIAGNOSTICS ?= 0
ifneq ($(DIAGNOSTICS),$(filter $(DIAGNOSTICS),0 1))
$(error DIAGNOSTICS must be 0 or 1)
endif
CFLAGS_PPU := -std=gnu99 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mminimal-toc -ffunction-sections -fdata-sections -fstack-usage -Iinclude -I$(PSL1GHT)/ppu/include
# PPU GCC 7.2 ICEs at -Os. LTO removes unused TLS routines before the
# PowerPC function descriptors can retain them. Native ABI adapters stay non-LTO.
TLS_CFLAGS_PPU := $(filter-out -O2,$(CFLAGS_PPU)) -O1 -flto -fno-fat-lto-objects
CFLAGS_PPU += -DPRESENCE_DIAGNOSTICS=$(DIAGNOSTICS)
TLS_CFLAGS_PPU += -DPRESENCE_DIAGNOSTICS=$(DIAGNOSTICS)
# GCC 7 warns across Mbed TLS's checked ECDH output-length call under LTO.
# Keep this library-only warning visible; project sources remain -Werror.
LINK_PPU := $(CC_PPU) $(TLS_CFLAGS_PPU) -Wno-error=maybe-uninitialized -save-temps=obj -fuse-linker-plugin -nostdlib
TLS_DIR := deps/mbedtls-3.6.6
TLS_FLAGS := -I$(TLS_DIR)/include -Ibuild -DMBEDTLS_CONFIG_FILE='"mbedtls_ps3_config.h"'
NET_NAMES := roots memory_owner webman_status arena utc websocket websocket_stream transport artwork_tls presence_clock
DISCORD_NAMES := config wire gateway gateway_json client artwork artwork_session
PS3_NAMES := network strings diagnostics webman
OBJS := build/session.o build/plugin.o build/module.o $(addprefix build/,$(addsuffix .o,$(NET_NAMES) $(PS3_NAMES) $(DISCORD_NAMES)))
TLS_NAMES := aes asn1parse asn1write base64 bignum bignum_core bignum_mod bignum_mod_raw cipher cipher_wrap constant_time ctr_drbg ecdh ecdsa ecp ecp_curves gcm md oid pem pk pkparse pk_wrap pk_ecc platform platform_util rsa rsa_alt_helpers sha1 sha256 sha512 ssl_ciphersuites ssl_client ssl_msg ssl_tls ssl_tls12_client x509 x509_crt x509_create
TLS_OBJS := $(addprefix build/tls/,$(addsuffix .o,$(TLS_NAMES)))
LIBGCC = $(shell $(CC_PPU) -print-libgcc-file-name)

.PHONY: all installer notices inspect clean
all: installer
installer: dist/ps3_presence.sprx notices
	$(MAKE) -C app all
notices: | dist
	rm -rf dist/licenses
	mkdir -p dist/licenses
	cp LICENSE licenses/*.txt dist/licenses/

build dist build/tls:
	mkdir -p $@
build/diagnostics-$(DIAGNOSTICS): | build
	rm -f build/diagnostics-*
	touch $@
$(OBJS) $(TLS_OBJS): build/diagnostics-$(DIAGNOSTICS)
build/roots.h: certs/gts-roots.pem tools/fetch-deps.py tools/embed_roots.py | build
	$(PYTHON) tools/fetch-deps.py
build/%.o: net/%.c build/roots.h include/mbedtls_ps3_config.h Makefile
	$(CC_PPU) $(CFLAGS_PPU) $(DEPFLAGS) $(TLS_FLAGS) -c $< -o $@
build/%.o: platform/ps3/%.c build/roots.h include/mbedtls_ps3_config.h Makefile
	$(CC_PPU) $(CFLAGS_PPU) $(DEPFLAGS) $(TLS_FLAGS) -c $< -o $@
build/%.o: discord/%.c build/roots.h include/mbedtls_ps3_config.h Makefile
	$(CC_PPU) $(CFLAGS_PPU) $(DEPFLAGS) $(TLS_FLAGS) -c $< -o $@
# Measured smaller at -O1 on PPU GCC 7.2; -Os ICEs for these functions.
build/session.o build/webman_status.o: CFLAGS_PPU := $(filter-out -O2,$(CFLAGS_PPU)) -O1
build/tls/%.o: build/roots.h include/mbedtls_ps3_config.h include/tls_port.h Makefile | build/tls
	$(CC_PPU) $(TLS_CFLAGS_PPU) $(DEPFLAGS) $(TLS_FLAGS) -I$(TLS_DIR)/library -Wno-error -c $(TLS_DIR)/library/$*.c -o $@
build/session.o: src/session.c Makefile | build
	$(CC_PPU) $(CFLAGS_PPU) $(DEPFLAGS) -c $< -o $@
build/plugin.o: platform/ps3/plugin.c Makefile | build
	$(CC_PPU) $(CFLAGS_PPU) $(DEPFLAGS) -c $< -o $@
build/module.o: platform/ps3/module.S Makefile | build
	$(CC_PPU) -c $< -o $@
build/presence.elf: $(OBJS) $(TLS_OBJS) platform/ps3/prx.ld
	$(LINK_PPU) -T platform/ps3/prx.ld -Wl,--gc-sections,--emit-relocs,-Map,build/presence.map -o $@ $(OBJS) $(TLS_OBJS) $(LIBGCC)
dist/ps3_presence.sprx: build/presence.elf tools/pack_prx.py | dist
	$(PYTHON) tools/pack_prx.py $< dist/ps3_presence.prx $@
inspect: dist/ps3_presence.sprx
	$(PYTHON) tools/verify_prx.py build/presence.elf dist/ps3_presence.prx dist/ps3_presence.sprx
clean:
	rm -rf build dist

$(OBJS:.o=.d) $(TLS_OBJS:.o=.d): ;
-include $(wildcard $(OBJS:.o=.d) $(TLS_OBJS:.o=.d))
