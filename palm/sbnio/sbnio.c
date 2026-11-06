/* SB NIO: exercise the springboard-nio CS1 link and talk to fujinet-nio.
 *
 * fujinet-nio-lib finds the module with Handspring's card attributes (CsBase,
 * CsSize), never a fixed address; the link is plain 16-bit MOVE.W reads and
 * writes on CS1. See include/sbnio.h. Each line shown is also sent to the
 * RP2350 console. */
#include <PalmOS.h>
#include "sbnio_rsc.h"
#include "sbnio.h"
#include "fujinet-nio.h"

/* Handspring extension calls through the HsSelector trap (HsExt.h). */
Err HsCardAttrGet(UInt16 cardNo, UInt16 attr, void *valueP)
    __attribute__((__callseq__("move.w #6,-(%%sp); trap #15; dc.w 0xA349")));
Err HsCardAttrSet(UInt16 cardNo, UInt16 attr, void *valueP)
    __attribute__((__callseq__("move.w #7,-(%%sp); trap #15; dc.w 0xA349")));
MemPtr *HsCardCatchListP(void)
    __attribute__((__callseq__("move.w #9,-(%%sp); trap #15; dc.w 0xA349")));

enum { hsAttrAccessTime = 5 };

/* Bus access time to ask for: slower than the ROM's 330 ns gives the RP2350
 * more time to answer each read. CS0 and CS1 share it. */
#define ACCESS_NS 450UL
#define LINES 11
#define LINE_CHARS 40
#define UNIX_TO_PALM 2082844800UL
#define ECHO_WORDS 512
#define ECHO_MAX_BAD 32
#define BUF_MAX 4096
#define LOOP_FIXED 9
#define LOOP_RANDOM 8
#define WAIT_MS 50

static char gLines[LINES][LINE_CHARS];
static UInt16 gLineCount;
static UInt32 gNs, gRnd = 1;
static void (*gTask)(void);
static UInt8 gReq[BUF_MAX], gResp[BUF_MAX];

static void draw(void)
{
    RectangleType r;
    UInt16 i, first = gLineCount > LINES ? gLineCount - LINES : 0;
    r.topLeft.x = 0; r.topLeft.y = 16; r.extent.x = 160; r.extent.y = 129;
    WinEraseRectangle(&r, 0);
    for (i = first; i < gLineCount; i++) {
        const char *s = gLines[i % LINES];
        WinDrawChars(s, StrLen(s), 1, 16 + (i - first) * 11);
    }
}

static void say(const char *s)
{
    StrNCopy(gLines[gLineCount % LINES], s, LINE_CHARS - 1);
    gLines[gLineCount % LINES][LINE_CHARS - 1] = 0;
    gLineCount++;
    draw();
    fn_palmos_log(s);
}

static void two(char *out, UInt16 v)
{
    out[0] = (char)('0' + v / 10 % 10);
    out[1] = (char)('0' + v % 10);
    out[2] = 0;
}

/* Pseudo-random test data; the state carries on so each press differs. */
static UInt16 rnd(void)
{
    gRnd = gRnd * 1664525UL + 1013904223UL;
    return (UInt16)(gRnd >> 16);
}

static const char *res_name(UInt16 r)
{
    switch (r) {
    case SBN_RES_OK: return "ok";
    case SBN_RES_NO_HOST: return "no host";
    case SBN_RES_TIMEOUT: return "timeout";
    case SBN_RES_BAD_LEN: return "bad len";
    case SBN_RES_BAD_CHECK: return "bad check";
    case SBN_RES_TOO_BIG: return "too big";
    case SBN_RES_ABORTED: return "aborted";
    case FN_PALMOS_NO_ANSWER: return "no answer";
    case FN_PALMOS_BAD_READ: return "bad read";
    }
    return "?";
}

/* A register read right after a write may not have caught up yet, so poll. */
static Boolean wait_reg(UInt16 off, UInt16 mask, UInt16 want)
{
    UInt32 start = TimGetTicks(), limit = WAIT_MS * (UInt32)SysTicksPerSecond() / 1000;

    while ((fn_palmos_read(off) & mask) != want)
        if (TimGetTicks() - start > limit) return false;
    return true;
}

/* Run fn with Handspring's removal handler armed, so pulling the module
 * mid-test unwinds here instead of crashing. */
static Err guarded(void (*fn)(void))
{
    ErrExceptionType frame;
    MemPtr *list = HsCardCatchListP();
    volatile Err err = 0;

    if (!list) {
        fn();
        return 0;
    }
    frame.err = 0;
    frame.nextP = (ErrExceptionPtr)*list;
    *list = (MemPtr)&frame;
    if (ErrSetJump(frame.state) == 0)
        fn();
    else
        err = frame.err ? frame.err : 1;
    *list = (MemPtr)frame.nextP;
    return err;
}

/* FujiBus: device, command, u16 LE length, checksum, descriptors... */
static UInt8 fb_sum(const UInt8 *p, UInt16 n)
{
    UInt16 s = 0, i;
    for (i = 0; i < n; i++) {
        s += i == 4 ? 0 : p[i];
        s = (s >> 8) + (s & 0xFF);
    }
    return (UInt8)s;
}

static UInt16 fb_request(UInt8 *p, UInt8 device, UInt8 command)
{
    p[0] = device; p[1] = command; p[2] = 6; p[3] = 0; p[4] = 0; p[5] = 0;
    p[4] = fb_sum(p, 6);
    return 6;
}

/* Check a reply and return its NIO status (or 0xFF), payload after it. */
static UInt8 fb_reply(UInt16 n, UInt8 device, UInt8 command, const UInt8 **payload, UInt16 *payloadLen)
{
    if (n < 7 || gResp[0] != device || gResp[1] != command ||
        (UInt16)(gResp[2] | gResp[3] << 8) != n || fb_sum(gResp, n) != gResp[4] || gResp[5] != 1)
        return 0xFF;
    *payload = gResp + 7;
    *payloadLen = n - 7;
    return gResp[6];
}

static void probe(void)
{
    UInt16 t1, t2;
    char buf[48];

    StrPrintF(buf, "card %u CS0 %lx", fn_palmos_card(), fn_palmos_cs0());
    say(buf);
    StrPrintF(buf, "CS1 %lx size %lx", fn_palmos_cs1(), fn_palmos_cs1() - fn_palmos_cs0());
    say(buf);
    StrPrintF(buf, "access time %lu ns", gNs);
    say(buf);
    StrPrintF(buf, "magic %x ver %x status %x", fn_palmos_read(SBN_R_MAGIC), fn_palmos_read(SBN_R_VERSION),
              fn_palmos_read(SBN_R_STATUS));
    say(buf);
    StrPrintF(buf, "writes %x", fn_palmos_read(SBN_R_WRITES));
    say(buf);
    t1 = fn_palmos_read(SBN_R_TICKS);
    SysTaskDelay(SysTicksPerSecond() / 10);
    t2 = fn_palmos_read(SBN_R_TICKS);
    StrPrintF(buf, "ticks %x>%x %s", t1, t2, t1 != t2 ? "(live)" : "(STUCK)");
    say(buf);
}

/* All 1s then all 0s (a reset leaves ECHO at 0, which would pass unchecked),
 * each bit walking through 0s and through 1s, then pseudo-random words. */
static UInt16 echo_value(UInt16 n)
{
    if (n == 0) return 0xFFFF;
    if (n == 1) return 0x0000;
    if (n < 18) return (UInt16)(1U << (n - 2));
    if (n < 34) return (UInt16)~(1U << (n - 18));
    return rnd();
}

/* Does every write the Palm makes to CS1 arrive with all 16 bits intact? */
static void echo(void)
{
    UInt16 i, v, bad = 0, badAt = 0, badWrote = 0, badRead = 0, before = fn_palmos_read(SBN_R_WRITES);
    UInt32 t0 = TimGetTicks(), ms;
    char buf[48];

    for (i = 0; i < ECHO_WORDS && bad < ECHO_MAX_BAD; i++) {
        v = echo_value(i);
        fn_palmos_write(SBN_W_ECHO, v);
        if (wait_reg(SBN_R_ECHO, 0xFFFF, v)) continue;
        if (!bad++) {
            badAt = i;
            badWrote = v;
            badRead = fn_palmos_read(SBN_R_ECHO);
        }
    }
    ms = (TimGetTicks() - t0) * 1000 / SysTicksPerSecond();
    /* Count before logging: say() itself writes to CS1. */
    wait_reg(SBN_R_WRITES, 0xFFFF, (UInt16)(before + i));
    v = fn_palmos_read(SBN_R_WRITES) - before;
    StrPrintF(buf, "echo %u/%u ok in %lu ms", i - bad, i, ms);
    say(buf);
    if (bad) {
        StrPrintF(buf, "bad #%u wrote %x read %x", badAt, badWrote, badRead);
        say(buf);
    }
    if (v == i)
        StrPrintF(buf, "writes +%u ok", v);
    else
        StrPrintF(buf, "writes +%u, expected %u", v, i);
    say(buf);
}

static Boolean loopback(Boolean on)
{
    fn_palmos_write(SBN_W_CONTROL, on ? SBN_CTL_LOOPBACK_ON : SBN_CTL_LOOPBACK_OFF);
    return wait_reg(SBN_R_STATUS, SBN_ST_LOOPBACK, on ? SBN_ST_LOOPBACK : 0);
}

static void fill(UInt8 *p, UInt16 n)
{
    UInt16 w;

    for (; n > 1; n -= 2, p += 2) {
        w = rnd();
        p[0] = (UInt8)(w >> 8);
        p[1] = (UInt8)w;
    }
    if (n) *p = (UInt8)(rnd() >> 8);
}

/* Round trips with the firmware echoing each request back (no NIO), so the
 * link itself is what is tested, in both directions, at many lengths. */
static void loop(void)
{
    static const UInt16 lens[LOOP_FIXED] = { 1, 2, 3, 255, 256, 511, 1024, 4095, 4096 };
    UInt16 n, i, len, got, result = SBN_RES_OK, ok = 0, badLen = 0, badResult = 0, badGot = 0, badAt = 0;
    UInt32 t, ticks = 0, bytes = 0, ms;
    char buf[48];

    if (!loopback(true)) {
        say("loopback did not turn on");
        loopback(false);
        return;
    }
    /* Stop at the first unanswered request: each one waits 35 s. */
    for (n = 0; n < LOOP_FIXED + LOOP_RANDOM && result != FN_PALMOS_NO_ANSWER; n++) {
        len = n < LOOP_FIXED ? lens[n] : (UInt16)(1 + (rnd() & (BUF_MAX - 1)));
        fill(gReq, len);
        /* Time only the exchanges, not filling the request. */
        t = TimGetTicks();
        result = fn_palmos_exchange(gReq, len, gResp, BUF_MAX, &got);
        ticks += TimGetTicks() - t;
        bytes += len;
        if (result == SBN_RES_OK && got == len && MemCmp(gReq, gResp, len) == 0) {
            ok++;
            continue;
        }
        if (!badLen) {
            for (i = 0; i < len && i < got && gReq[i] == gResp[i]; i++)
                ;
            badLen = len;
            badResult = result;
            badGot = got;
            badAt = i;
        }
    }
    if (!loopback(false)) say("loopback stuck on");
    ms = ticks * 1000 / SysTicksPerSecond();
    if (!ms) ms = 1;
    StrPrintF(buf, "loop %u/%u ok", ok, n);
    say(buf);
    if (badLen) {
        StrPrintF(buf, "bad len %u: %s, got %u", badLen, res_name(badResult), badGot);
        say(buf);
        StrPrintF(buf, "first difference at byte %u", badAt);
        say(buf);
    }
    StrPrintF(buf, "%lu bytes each way, %lu ms", bytes, ms);
    say(buf);
    StrPrintF(buf, "%lu bytes/s each way", bytes * 1000 / ms);
    say(buf);
}

static void nio_time(void)
{
    UInt16 got, len, result;
    const UInt8 *p;
    UInt8 status;
    UInt32 seconds;
    DateTimeType dt;
    char buf[48], a[3], b[3], c[3], d[3], e[3];

    result = fn_palmos_exchange(gReq, fb_request(gReq, 0x45, 0x01), gResp, BUF_MAX, &got);
    if (result != SBN_RES_OK) {
        StrPrintF(buf, "GetTime: link %s", res_name(result));
        say(buf);
        return;
    }
    status = fb_reply(got, 0x45, 0x01, &p, &len);
    if (status || len < 12) {
        StrPrintF(buf, "GetTime: bad reply, status %u", status);
        say(buf);
        return;
    }
    seconds = (UInt32)p[4] | (UInt32)p[5] << 8 | (UInt32)p[6] << 16 | (UInt32)p[7] << 24;
    TimSecondsToDateTime(seconds + UNIX_TO_PALM, &dt);
    two(a, dt.month); two(b, dt.day); two(c, dt.hour); two(d, dt.minute); two(e, dt.second);
    StrPrintF(buf, "NIO time %u-%s-%s %s:%s:%s UTC", dt.year, a, b, c, d, e);
    say(buf);

    result = fn_palmos_exchange(gReq, fb_request(gReq, 0x45, 0x04), gResp, BUF_MAX, &got);
    status = result == SBN_RES_OK ? fb_reply(got, 0x45, 0x04, &p, &len) : 0xFF;
    if (!status && len >= 2 && p[1] < 30 && 2 + p[1] <= len) {
        StrCopy(buf, "NIO timezone ");
        MemMove(buf + 13, p + 2, p[1]);
        buf[13 + p[1]] = 0;
        say(buf);
    }
}

/* Open the module on first use (and ask for the slower bus access time),
 * then run the chosen test. */
static void task(void)
{
    if (!fn_palmos_cs1()) {
        if (!fn_palmos_open()) {
            say("No Springboard module (ABI 4)");
            return;
        }
        gNs = ACCESS_NS;
        HsCardAttrSet(fn_palmos_card(), hsAttrAccessTime, &gNs);
        if (HsCardAttrGet(fn_palmos_card(), hsAttrAccessTime, &gNs)) gNs = 0;
    }
    gTask();
}

static void run(void (*fn)(void))
{
    char buf[40];
    Err err;

    gTask = fn;
    err = guarded(task);
    if (err) {
        /* Forget the module first: say() would log to it. */
        fn_palmos_close();
        StrPrintF(buf, "Module removed (err %x)", err);
        say(buf);
    }
}

static Boolean MainFormHandleEvent(EventType *e)
{
    if (e->eType == frmOpenEvent) {
        FrmDrawForm(FrmGetActiveForm());
        run(probe);
        return true;
    }
    if (e->eType == ctlSelectEvent) {
        switch (e->data.ctlSelect.controlID) {
        case BtnProbe: run(probe); return true;
        case BtnEcho: run(echo); return true;
        case BtnLoop: run(loop); return true;
        case BtnTime: run(nio_time); return true;
        }
    }
    return false;
}

static Boolean AppHandleEvent(EventType *e)
{
    if (e->eType == frmLoadEvent) {
        FormType *frm = FrmInitForm(e->data.frmLoad.formID);
        FrmSetActiveForm(frm);
        FrmSetEventHandler(frm, MainFormHandleEvent);
        return true;
    }
    return false;
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
    EventType e;
    UInt16 err;

    if (cmd != sysAppLaunchCmdNormalLaunch)
        return 0;
    FrmGotoForm(MainForm);
    do {
        EvtGetEvent(&e, evtWaitForever);
        if (SysHandleEvent(&e)) continue;
        if (MenuHandleEvent(0, &e, &err)) continue;
        if (AppHandleEvent(&e)) continue;
        FrmDispatchEvent(&e);
    } while (e.eType != appStopEvent);
    FrmCloseAllForms();
    return 0;
}
