# Run inside the sbnio-palm container (./build.sh). Builds fujinet-nio-lib's
# palmos-springboard target, the Palm apps on it and a Springboard ROM carrying
# the apps, using Handspring's Palm-MakeROM from mozzwald's fujinet-palm checkout.
CC = m68k-palmos-gcc
CFLAGS = -O2 -Wall -palmos3.5 -I../include
NIODIR = ../repos/fujinet-nio-lib
NIOLIB = $(NIODIR)/build/fujinet-nio-palmos-springboard.a
NIOFLAGS = -I$(NIODIR)/include/palmos -I$(NIODIR)/include
MAKEROM_SRC = ../repos/fujinet-palm/tools/makerom
SDK_INC = /opt/palmdev/sdk-3.5/include
B = build
# The module ROM holds up to 64 KB; these five take about 62 KB.
APPS = sbnio nionet ficonfig isstracker weather
FNPALM = ../repos/fujinet-palm/palm/apps
COMPAT = compat/fnapp.c compat/fn_classic.c
# fujinet-palm's nio branch, for FI Config (build.sh mounts it).
FI_PALM ?= ../repos/fujinet-palm-nio
FICONFIG = $(abspath $(FI_PALM))/apps/FujiConfig

all: $(B)/sbnio.rom

$(B)/Palm-MakeROM: $(wildcard $(MAKEROM_SRC)/*.cpp $(MAKEROM_SRC)/*.h)
	mkdir -p $(B)
	g++ -m32 -std=gnu++98 -fpermissive -w -DCPU_TYPE=1 -DEMULATION_LEVEL=1 -D_strdup=strdup \
	  -I$(MAKEROM_SRC) $$(find $(SDK_INC) -type d -printf '-I%p ') \
	  $(MAKEROM_SRC)/Main.cpp $(MAKEROM_SRC)/Utility.cpp $(MAKEROM_SRC)/Crc.cpp -o $@

$(NIOLIB): FORCE
	$(MAKE) -C $(NIODIR) palmos-springboard SBNIO_ABI_INCLUDE=-I$(abspath ../include)

$(B)/sbnio/sbnio: sbnio/sbnio.c sbnio/sbnio_rsc.h ../include/sbnio.h $(NIOLIB)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(NIOFLAGS) -o $@ sbnio/sbnio.c $(NIOLIB)

$(B)/nionet/nionet: nionet/nionet.c nionet/nionet_rsc.h $(NIOLIB)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(NIOFLAGS) -o $@ nionet/nionet.c $(NIOLIB)

# FI Config from fujinet-palm, without its HotSync queue (FI_NO_HOTSYNC): it
# installs straight into RAM with ExgDBRead. Same name and creator as on the
# cradle. FN_MAX_PACKET_SIZE is the library's for palmos-springboard (its
# default, 1024).
$(B)/ficonfig/ficonfig: $(FICONFIG)/FujiConfig.c $(FICONFIG)/filedev.c $(wildcard $(FICONFIG)/*.h) $(NIOLIB)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(NIOFLAGS) -DFN_MAX_PACKET_SIZE=1024 -DFI_NO_HOTSYNC '-DAPP_CREATOR_STR="ANHS"' -I$(FICONFIG) \
	  -I$(abspath $(FI_PALM))/common -o $@ $(FICONFIG)/FujiConfig.c $(FICONFIG)/filedev.c $(NIOLIB)

$(B)/ficonfig/bin.stamp: $(FICONFIG)/FujiConfig.rcp $(FICONFIG)/FujiConfigRsc.h $(FICONFIG)/icon.bmp
	mkdir -p $(@D)
	cd $(@D) && rm -f *.bin && pilrc -q -D FI_NO_HOTSYNC -I $(FICONFIG) $(FICONFIG)/FujiConfig.rcp
	touch $@

$(B)/ficonfig.prc: $(B)/ficonfig/ficonfig $(B)/ficonfig/bin.stamp
	cd $(B)/ficonfig && build-prc -n "FI Config" -c ANHS -o ../ficonfig.prc ficonfig *.bin

# fujinet-palm apps, unchanged, on the classic-API shim in compat/.
define fnpalm_app
$(B)/$(1)/$(1): $(FNPALM)/$(1)/*.c $(COMPAT) $(NIOLIB)
	mkdir -p $(B)/$(1)
	$(CC) $(CFLAGS) -Icompat $(NIOFLAGS) -I$(FNPALM)/$(1) -o $$@ $(FNPALM)/$(1)/*.c $(COMPAT) $(NIOLIB)

$(B)/$(1)/bin.stamp: $(FNPALM)/$(1)/$(1).rcp
	mkdir -p $(B)/$(1)
	cd $(B)/$(1) && rm -f *.bin && pilrc -q -I ../../$(FNPALM)/$(1) ../../$(FNPALM)/$(1)/$(1).rcp
	touch $$@

$(B)/$(1).prc: $(B)/$(1)/$(1) $(B)/$(1)/bin.stamp
	cd $(B)/$(1) && build-prc -o ../$(1).prc ../../$(FNPALM)/$(1)/$(1).def $(1) *.bin
endef
$(foreach a,isstracker weather,$(eval $(call fnpalm_app,$(a))))

# PilRC output is per app: both apps' forms are tFRM 1000.
define app
$(B)/$(1)/bin.stamp: $(1)/$(1).rcp $(1)/$(1)_rsc.h
	mkdir -p $(B)/$(1)
	cd $(B)/$(1) && rm -f *.bin && pilrc -q -I ../../$(1) ../../$(1)/$(1).rcp
	touch $$@

$(B)/$(1).prc: $(B)/$(1)/$(1) $(B)/$(1)/bin.stamp $(1)/$(1).def
	cd $(B)/$(1) && build-prc -o ../$(1).prc ../../$(1)/$(1).def $(1) *.bin
endef
$(foreach a,sbnio nionet,$(eval $(call app,$(a))))

# Same card layout and HsAT 330 ns token as mozzwald's proven ROM-only image.
$(B)/sbnio.rom: $(patsubst %,$(B)/%.prc,$(APPS)) $(B)/Palm-MakeROM
	$(B)/Palm-MakeROM -op create -hdr 0x08000000 -romBlock 0x08000000 0x10000 \
	  -chChecksum -chName "Springboard NIO" -chManuf "FujiNet" -chVersion 0x0100 \
	  -chZCrDate -romName "SBNIO 0.4" -autoSize -tokStr HsAT 330 \
	  $(patsubst %,-romDB $(B)/%.prc,$(APPS)) -o $@

clean:
	rm -rf $(B)
	$(MAKE) -C $(NIODIR) clean

.PHONY: all clean FORCE
