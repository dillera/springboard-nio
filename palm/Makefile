# Run inside the sbnio-palm container (./build.sh). Builds the Palm apps, the
# fujinet-nio-lib port and a Springboard ROM carrying the apps, using
# Handspring's Palm-MakeROM from mozzwald's fujinet-palm checkout.
CC = m68k-palmos-gcc
CFLAGS = -O2 -Wall -palmos3.5 -I../include -Icommon
NIOLIB = nio-lib/build/libfujinetnio.a
NIOFLAGS = -Inio-lib/shim -I../repos/fujinet-nio-lib/include
MAKEROM_SRC = ../repos/fujinet-palm/tools/makerom
SDK_INC = /opt/palmdev/sdk-3.5/include
B = build
APPS = sbnio nionet isstracker weather
FNPALM = ../repos/fujinet-palm/palm/apps
COMPAT = compat/fnapp.c compat/fn_classic.c

all: $(B)/sbnio.rom

$(B)/Palm-MakeROM: $(wildcard $(MAKEROM_SRC)/*.cpp $(MAKEROM_SRC)/*.h)
	mkdir -p $(B)
	g++ -m32 -std=gnu++98 -fpermissive -w -DCPU_TYPE=1 -DEMULATION_LEVEL=1 -D_strdup=strdup \
	  -I$(MAKEROM_SRC) $$(find $(SDK_INC) -type d -printf '-I%p ') \
	  $(MAKEROM_SRC)/Main.cpp $(MAKEROM_SRC)/Utility.cpp $(MAKEROM_SRC)/Crc.cpp -o $@

$(NIOLIB): FORCE
	$(MAKE) -C nio-lib

$(B)/sbnio/sbnio: sbnio/sbnio.c sbnio/sbnio_rsc.h common/sbn_client.c common/sbn_client.h ../include/sbnio.h
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ sbnio/sbnio.c common/sbn_client.c

$(B)/nionet/nionet: nionet/nionet.c nionet/nionet_rsc.h $(NIOLIB)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(NIOFLAGS) -o $@ nionet/nionet.c $(NIOLIB)

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
	  -chZCrDate -romName "SBNIO 0.3" -autoSize -tokStr HsAT 330 \
	  $(patsubst %,-romDB $(B)/%.prc,$(APPS)) -o $@

clean:
	rm -rf $(B)
	$(MAKE) -C nio-lib clean

.PHONY: all clean FORCE
