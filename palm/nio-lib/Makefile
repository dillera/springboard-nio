# fujinet-nio-lib for Palm OS (prc-tools), with the springboard-nio mailbox
# as its transport. Run inside the sbnio-palm container.
CC = m68k-palmos-gcc
AR = m68k-palmos-ar
LIB = ../../repos/fujinet-nio-lib
CFLAGS = -O2 -Wall -palmos3.5 -Ishim -I$(LIB)/include -I../../include -I../common
B = build
SRCS = $(filter-out %/fn_transport_stream.c,$(wildcard $(LIB)/src/common/*.c))
OBJS = $(patsubst $(LIB)/src/common/%.c,$(B)/%.o,$(SRCS)) $(B)/fn_transport_palm.o $(B)/sbn_client.o

all: $(B)/libfujinetnio.a

$(B)/%.o: $(LIB)/src/common/%.c
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/fn_transport_palm.o: fn_transport_palm.c ../common/sbn_client.h ../../include/sbnio.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/sbn_client.o: ../common/sbn_client.c ../common/sbn_client.h ../../include/sbnio.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/libfujinetnio.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

clean:
	rm -rf $(B)
.PHONY: all clean
