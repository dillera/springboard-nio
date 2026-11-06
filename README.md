# springboard-nio

A Handspring Visor Prism talking to [fujinet-nio](https://github.com/markjfisher/fujinet-nio)
through a Springboard module: a Waveshare Core2350B (RP2350B) on mozzwald's
Springboard carrier, with NIO running on this Mac over the RP2350's USB.

```
Prism (Palm OS 3.5)  --Springboard CS0/CS1-->  RP2350  --USB serial, packet link-->  fujinet-nio (Mac)
  SB NIO app            ROM / two-way link     firmware/      FujiBus                   fujibus-link-serial-debug
```

The two chip selects (`include/sbnio.h`, ABI 4):

- **CS0** is the Springboard ROM image (up to 64 KB), served from SRAM. It holds
  the apps, so the Prism lists them as soon as it recognises the module.
- **CS1** (CsBase + CsSize, 0x29000000 on the Prism) is the link, with plain
  MOVE.W cycles both ways:
  - **write** to CS1 + (register << 16): A16-A23 pick the register (data,
    begin, check, doorbell, control, log, echo) and all 16 data bits count,
    so each write carries two request bytes.
  - **read** from CS1 + offset: registers from 0x0000, the reply at 0x1000,
    from a table the firmware keeps current, so reads have no side effects.
- A request is BEGIN with its length, the raw FujiBus packet two bytes per DATA
  write, CHECK, then DOORBELL with a sequence. The firmware checks length and
  check word (a mismatch answers BAD_CHECK and the Palm resends), sends the
  packet over NIO's packet link on USB port 0 (CRC-checked records in a
  generation agreed with Sync/SyncAck, `firmware/src/packet_link.c`), and
  publishes NIO's reply with its own check word, which the Palm verifies.

The read engine is mozzwald's hardware-proven Experiment 2 (`fujinet-nio`
branch `experiment/dual-cs-write-capture`): PIO1 owns the data pads and Core1
answers each read from SRAM, with its loop moved into SCRATCH_X. Writes are
caught by an input-only PIO2 state machine running with GPIO base 16, so one
snapshot holds the selects, D0-D15 and A16-A23 together; DMA streams the
snapshots into a ring for Core0. Nothing new ever drives the bus. A second
PIO2 machine times WE low on every CS1 write (`status`, `widths`).

## Layout

| Path | |
| --- | --- |
| `include/sbnio.h` | CS1 link ABI shared by the firmware and the Palm apps |
| `firmware/` | RP2350 firmware (pico-sdk 2.3.1, Arm GNU 14.2) |
| `palm/` | Palm apps and the Springboard ROM build (Docker `sbnio-palm`) |
| `palm/compat/` | classic fujinet-lib network calls on fujinet-nio-lib, for fujinet-palm apps |
| `palm/nionet/` | NIO Net: HTTP, HTTPS and JSON-translation tests |
| `host/` | Mac tools: `flash.sh`, `nio.sh`, `sbcon.py` (firmware console) |
| `rom/` | local ROM images, not tracked (mozzwald's `baseline-v3.rom` comes from his private repo) |
| `docs/hardware/` | carrier pinout |
| `repos/` | upstream checkouts (not tracked): mozzwald's `fujinet-nio` (`visor`), `fujinet-palm` (`visor`) and the private `handspring-visor-plan`; `fujinet-nio-lib` (`master`, its `palmos-springboard` target); `fujinet-nio-master` (fujinet-nio `master`, the NIO that runs); `fujinet-palm-nio` (dillera/fujinet-palm `nio`, for FI Config) |


## Building and running

Prerequisites: Arm GNU toolchain 14.2 in `~/opt/arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi`,
`picotool`, Docker with the `palmdev` image, the pinned pico-sdk/apio under
`repos/fujinet-nio/bridges/rp2350-springboard/.deps`, and a fujinet-palm
checkout on its `nio` branch for FI Config: `repos/fujinet-palm-nio`, or set
`FI_PALM`.

```sh
palm/build.sh                 # palm/build/*.prc and sbnio.rom
host/flash.sh                 # build firmware with that ROM, flash, restart NIO
host/flash.sh --palm          # both
host/nio.sh start|stop|restart|status
host/sbcon.py                 # watch the firmware console
host/sbcon.py status pins regs
host/sbcon.py "send 450106004c00"   # Clock.GetTime straight to NIO, no Palm needed
```

NIO is fujinet-nio's `master` (the packet link merged there), built with
`cmake --preset fujibus-link-serial-debug` in `repos/fujinet-nio-master` and
run in `host/nio-run/`; set `NIO` to a binary built elsewhere. The packet-link controller has a host test:

```sh
cc -Ifirmware/src firmware/tests/test_packet_link.c firmware/src/packet_link.c -o /tmp/pl && /tmp/pl
```

Console commands: `status`, `pins` (live bus levels), `watch`, `scan [ms]`,
`float`, `regs`, `send <hex>`, `trace on|off` (log every CS1 write),
`loopback on|off`, `settle [0-31]` (PIO cycles from seeing a write to sampling
it), `widths` (measured WE low times), `stop`, `arm`, `bootsel`.

## SB NIO app

Opens with a probe and shows its results; each line also goes to the console.

- **Probe**: CS0 and CS1 bases, then the link ID, ABI, status and write count, and checks the firmware clock moves.
- **Echo**: 512 words written to CS1 (all 0s and 1s, walking bits, random) and each read back.
- **Loop**: 17 requests of 1-4096 bytes, odd lengths included, echoed by the firmware (loopback) and compared byte for byte.
- **Time**: `Clock.GetTime` and `GetTimezone` from NIO on the Mac.

## Status (2026-10-28)

Working end to end on a Visor Prism: SB NIO runs from the module ROM, Probe
shows a live mailbox, Echo returns 256/256, Loop passes 40/40 round trips
(6,100 bytes each way in 390 ms) and Time gets `Clock.GetTime` and
`GetTimezone` from fujinet-nio on the Mac.

Lessons from bring-up:

- Two RP2350s were attached at first; `host/board.serial` pins the tools and
  `flash.sh` to the one on the carrier (2CB44E2E5B6BCA57).
- GP17 (/CS1) was unsoldered. The `float` console test (pull-down with the
  input buffer off, for RP2350-E9) shows which bus lines the Prism drives.
- The first link ran on CS0 alone (the Palm sent bytes by reading token
  addresses) while CS1 was unsoldered; it moved to CS1 once GP17 was fixed.
- Fatal Exceptions while the app ran from module ROM went away once Core1's
  reply loop moved to SCRATCH_X and Core0 stopped rewriting the table
  continuously: Core0's SRAM traffic was delaying CS0 replies.
- The app asks for a 450 ns access time, but the Prism reports 330 back; it
  works at 330.

Networking (later the same day): fujinet-nio-lib runs on the Palm over the
mailbox. NIO Net gets HTTP 200 in 460 ms and HTTPS 200 in 650 ms, and the ISS
position through NIO's JSON translator; fujinet-palm's ISS Tracker and Weather
run unchanged on the classic-API shim. 1,610 requests, no failures. The ROM
now holds four apps (48 KB); the mailbox moved to 0xE000 to make room.

## Two-way CS1 link (2026-10-30, branch `two-way-cs`)

The token mailbox is gone: the Palm writes requests to CS1 and reads replies
from CS1, and CS0 is only the ROM. On the Prism: Echo 512/512 in 90 ms, Loop
17/17 (25,708 bytes each way in 260 ms, about 99 KB/s each way against 16 KB/s
for the token mailbox), Time, and NIO Net HTTP 200 in 410 ms, HTTPS 200 in
660 ms and the ISS position in 3.4 s. 14,725 CS1 writes with no check
failures; WE stays low 340-406 ns per write and the firmware samples 53 ns
in. mozzwald's Experiment 3 had shown the Prism's CS1 writes can be captured;
this uses one GPIO-base-16 snapshot per write instead of pairing two.

The same day the firmware's SLIP framing to NIO's RS-232 build gave way to NIO's
packet link (`fujibus-link-serial-debug`). On the Prism: Echo 512/512 with the
write count right, Loop 17/17 at 99 KB/s, Time, and NIO Net reading the
577-byte HTTP page in 240 ms (was 410) and the HTTPS one in 370 ms (was 660).
111 requests in one link generation, all answered in 0-1 ms at the firmware,
with no resends, corrupt records or timeouts; 15,095 CS1 writes, no check
failures.

Next: News and Mastodon on the same shim, `host/tools/verify-rom.py` for
multi-app ROMs (it still expects one database), a RAM-resident install so a module
reflash cannot crash a running app, and the ESP32-S3 as USB host.

## FI Config in the module ROM (2026-11-05)

ROM "SBNIO 0.4" holds SB NIO, NIO Net, FI Config, ISS Tracker and Weather
(62,274 of 65,536 bytes). FI Config is fujinet-palm's, from its `nio` branch,
built with `FI_NO_HOTSYNC`: it browses a TNFS server through NIO's FileDevice
and installs a `.prc`, `.pdb` or `.pqa` straight into RAM with `ExgDBRead`,
asking before it replaces a copy already there. There is no HotSync queue,
since no HotSync stands behind the module.

On the Prism it installed FN FujiPet (31,571 bytes) from fujinet.diller.org in
22.6 s: 62 ReadFile requests of 512 bytes, about 0.36 s each. About 0.32 s of
that is NIO opening the TNFS file and seven read-ahead handles, one round trip
after another, then closing them all, because FileDevice opens the file again
for every read.
