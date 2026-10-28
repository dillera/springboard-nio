# Run inside the sbnio-palm container (./build.sh). Builds the SB NIO app and
# a Springboard ROM image carrying it, using Handspring's Palm-MakeROM from
# mozzwald's fujinet-palm checkout.
CC = m68k-palmos-gcc
CFLAGS = -O2 -Wall -palmos3.5 -I../include
MAKEROM_SRC = ../repos/fujinet-palm/tools/makerom
SDK_INC = /opt/palmdev/sdk-3.5/include
B = build

all: $(B)/sbnio.rom

$(B)/Palm-MakeROM: $(wildcard $(MAKEROM_SRC)/*.cpp $(MAKEROM_SRC)/*.h)
	mkdir -p $(B)
	g++ -m32 -std=gnu++98 -fpermissive -w -DCPU_TYPE=1 -DEMULATION_LEVEL=1 -D_strdup=strdup \
	  -I$(MAKEROM_SRC) $$(find $(SDK_INC) -type d -printf '-I%p ') \
	  $(MAKEROM_SRC)/Main.cpp $(MAKEROM_SRC)/Utility.cpp $(MAKEROM_SRC)/Crc.cpp -o $@

$(B)/sbnio: sbnio/sbnio.c sbnio/sbnio_rsc.h ../include/sbnio.h
	mkdir -p $(B)
	$(CC) $(CFLAGS) -o $@ sbnio/sbnio.c

$(B)/bin.stamp: sbnio/sbnio.rcp sbnio/sbnio_rsc.h
	mkdir -p $(B)
	cd $(B) && rm -f *.bin && pilrc -q -I ../sbnio ../sbnio/sbnio.rcp
	touch $@

$(B)/sbnio.prc: $(B)/sbnio $(B)/bin.stamp sbnio/sbnio.def
	cd $(B) && build-prc -o sbnio.prc ../sbnio/sbnio.def sbnio *.bin

# Same card layout and HsAT 330 ns token as mozzwald's proven ROM-only image.
$(B)/sbnio.rom: $(B)/sbnio.prc $(B)/Palm-MakeROM
	$(B)/Palm-MakeROM -op create -hdr 0x08000000 -romBlock 0x08000000 0x10000 \
	  -chChecksum -chName "Springboard NIO" -chManuf "FujiNet" -chVersion 0x0100 \
	  -chZCrDate -romName "SBNIO 0.1" -autoSize -tokStr HsAT 330 \
	  -romDB $(B)/sbnio.prc -o $@

clean:
	rm -rf $(B)

.PHONY: all clean
