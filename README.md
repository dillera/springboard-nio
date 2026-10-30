# springboard-nio

A Handspring Visor Prism talking to [fujinet-nio](https://github.com/markjfisher/fujinet-nio)
through a Springboard module: a Waveshare Core2350B (RP2350B) on mozzwald's
Springboard carrier, with NIO running on this Mac over the RP2350's USB.

```
Prism (Palm OS 3.5)  --Springboard CS0-->  RP2350  --USB serial, SLIP-->  fujinet-nio (Mac)
  SB NIO app           ROM + mailbox       firmware/      FujiBus            fujibus-rs232-debug
```

Everything uses the CS0 window (`include/sbnio.h`):

- **0x0000-0x7FFF**: the Springboard ROM image, served from SRAM. It holds the
  SB NIO app, so the Prism lists it as soon as it recognises the module.
- **0x8000-0xBFFF**: mailbox registers and the reply, read with MOVE.W from a
  table the firmware keeps current, so reads have no side effects.
- **0xC000-0xFFFF**: tokens. The Palm sends a byte by *reading* the address
  that encodes it; nothing is ever written to the bus. A request is BEGIN, one
  BYTE token per byte of the raw FujiBus packet, then DOORBELL.
- The firmware SLIP-frames each request onto USB port 0, where NIO's RS-232
  build is listening, and publishes NIO's reply in the register table.

The read engine is mozzwald's hardware-proven Experiment 2 (`fujinet-nio`
branch `experiment/dual-cs-write-capture`): PIO1 owns the data pads and Core1
answers each read from SRAM, with its loop moved into SCRATCH_X. Tokens are
caught by a new input-only PIO2 state machine (one GPIO snapshot per token
read, streamed by DMA into a ring), so nothing new ever drives the bus. CS1
reads still come from a second table, unused for now.

## Layout

| Path | |
| --- | --- |
| `include/sbnio.h` | CS1 register map shared by the firmware and the Palm app |
| `firmware/` | RP2350 firmware (pico-sdk 2.3.1, Arm GNU 14.2) |
| `palm/` | Palm apps and the Springboard ROM build (Docker `sbnio-palm`) |
| `palm/nio-lib/` | fujinet-nio-lib for Palm OS: shim headers and the mailbox transport |
| `palm/compat/` | classic fujinet-lib network calls on fujinet-nio-lib, for fujinet-palm apps |
| `palm/nionet/` | NIO Net: HTTP, HTTPS and JSON-translation tests |
| `host/` | Mac tools: `flash.sh`, `nio.sh`, `sbcon.py` (firmware console) |
| `rom/` | local ROM images, not tracked (mozzwald's `baseline-v3.rom` comes from his private repo) |
| `docs/hardware/` | carrier pinout |
| `repos/` | upstream checkouts (not tracked): mozzwald's `fujinet-nio` (`visor`), `fujinet-palm` (`visor`) and the private `handspring-visor-plan` |

The planning repo `repos/handspring-visor-plan` is private; keep it local.

## Building and running

Prerequisites: Arm GNU toolchain 14.2 in `~/opt/arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi`,
`picotool`, Docker with the `palmdev` image, and the pinned pico-sdk/apio under
`repos/fujinet-nio/bridges/rp2350-springboard/.deps`.

```sh
palm/build.sh                 # palm/build/sbnio.prc and sbnio.rom
host/flash.sh                 # build firmware with that ROM, flash, restart NIO
host/flash.sh --palm          # both
host/nio.sh start|stop|restart|status
host/sbcon.py                 # watch the firmware console
host/sbcon.py status pins regs
host/sbcon.py "send 450106004c00"   # Clock.GetTime straight to NIO, no Palm needed
```

NIO is `repos/fujinet-nio/build/fujibus-rs232-debug/fujinet-nio`
(`cmake --preset fujibus-rs232-debug`), run in `host/nio-run/`.

Console commands: `status`, `pins` (live bus levels), `regs`, `send <hex>`,
`trace on|off` (log every CS1 write), `loopback on|off`, `stop`, `arm`,
`bootsel`.

## SB NIO app

Opens with a probe and shows its results; each line also goes to the console.

- **Probe**: reads the mailbox ID, ABI and status, and checks the firmware clock moves.
- **Echo**: 256 echo tokens, each read back through the register table.
- **Loop**: 40 requests of 1-300 bytes echoed by the firmware (loopback), compared byte for byte.
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
- The mailbox moved to CS0 (reads plus token addresses), so CS1 is no longer
  needed. CS1 reads still come from the second table if wanted later.
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

Next: News and Mastodon on the same shim, a RAM-resident install so a module
reflash cannot crash a running app, and the ESP32-S3 as USB host.
