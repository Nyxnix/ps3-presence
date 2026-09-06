PS3DEV ?= /opt/ps3dev
PSL1GHT ?= $(PS3DEV)
export PS3DEV PSL1GHT
CC := $(PS3DEV)/ppu/bin/ppu-gcc
DEPFLAGS := -MMD -MP
CFLAGS := -std=gnu99 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -I../include -I$(PSL1GHT)/ppu/include
LIBS := -L$(PSL1GHT)/ppu/lib -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm
CONTENT := UP0001-PS3RPC001_00-PS3PRESENCE000001
OBJECTS := $(addprefix ../build/app/,$(addsuffix .o,main screen render settings config))
.PHONY: all
all: ../dist/ps3-presence-installer.pkg
../build/app ../dist:
	mkdir -p $@
../build/app/%.o: %.c Makefile | ../build/app
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@
../build/app/config.o: ../discord/config.c ../include/discord_wire.h Makefile | ../build/app
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@
../build/app/presence-app.elf: $(OBJECTS)
	$(CC) -Wl,-Map,../build/app/presence-app.map -o $@ $^ $(LIBS)
../dist/ps3-presence-installer.pkg: ../build/app/presence-app.elf ../dist/ps3_presence.sprx ICON0.PNG | ../dist
	mkdir -p ../build/app/pkg/USRDIR
	$(PS3DEV)/ppu/bin/ppu-strip $< -o ../build/app/stripped.elf
	$(PS3DEV)/bin/sprxlinker ../build/app/stripped.elf
	$(PS3DEV)/bin/make_self_npdrm ../build/app/stripped.elf ../build/app/pkg/USRDIR/EBOOT.BIN $(CONTENT)
	cp ../dist/ps3_presence.sprx ../build/app/pkg/USRDIR/ps3_presence.sprx
	cp ICON0.PNG ../build/app/pkg/ICON0.PNG
	$(PS3DEV)/bin/sfo --title 'PS3 Presence' --appid PS3RPC001 -f $(PS3DEV)/bin/sfo.xml ../build/app/pkg/PARAM.SFO
	$(PS3DEV)/bin/pkg --contentid $(CONTENT) ../build/app/pkg/ $@
	$(PS3DEV)/bin/package_finalize $@

-include $(OBJECTS:.o=.d)
