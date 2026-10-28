/* SB NIO: exercise the springboard-nio mailbox and talk to fujinet-nio.
 *
 * Everything goes through the module's CS0 window, found with Handspring's
 * card attributes (CsBase), never a fixed address. Registers are read with
 * 16-bit MOVE.W; bytes are sent by reading token addresses. See
 * include/sbnio.h. Each line shown is also sent to the RP2350 console. */
#include <PalmOS.h>
#include "sbnio_rsc.h"
#include "sbnio.h"

/* Handspring extension calls through the HsSelector trap (HsExt.h). */
Err HsCardAttrGet(UInt16 cardNo, UInt16 attr, void *valueP)
    __attribute__((__callseq__("move.w #6,-(%%sp); trap #15; dc.w 0xA349")));
Err HsCardAttrSet(UInt16 cardNo, UInt16 attr, void *valueP)
    __attribute__((__callseq__("move.w #7,-(%%sp); trap #15; dc.w 0xA349")));
MemPtr *HsCardCatchListP(void)
    __attribute__((__callseq__("move.w #9,-(%%sp); trap #15; dc.w 0xA349")));

enum { hsAttrRemovable = 0, hsAttrHwInstalled = 1, hsAttrCsBase = 3,
       hsAttrCsSize = 4, hsAttrAccessTime = 5 };

/* Bus access time to ask for: slower than the ROM's 330 ns gives the RP2350
 * more time to answer each read. CS0 and CS1 share it. */
#define ACCESS_NS 450UL
#define LINES 11
#define LINE_CHARS 40
#define UNIX_TO_PALM 2082844800UL

static char gLines[LINES][LINE_CHARS];
static UInt16 gLineCount;
static UInt32 gBase;
static UInt8 gSeq;
static UInt8 gReq[300], gResp[300];

static UInt16 gSink;

static UInt16 rd(UInt16 off) { return *(volatile UInt16 *)(gBase + off); }

/* Send one byte by reading the token address that encodes it. */
static void token(UInt16 type, UInt8 byte)
{
    gSink = *(volatile UInt16 *)(gBase + (UInt32)SBN_TOKEN(type, byte));
}

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
    const char *p;
    StrNCopy(gLines[gLineCount % LINES], s, LINE_CHARS - 1);
    gLines[gLineCount % LINES][LINE_CHARS - 1] = 0;
    gLineCount++;
    draw();
    if (gBase) {
        for (p = s; *p; p++) token(SBN_TK_LOG, (UInt8)*p);
        token(SBN_TK_LOG, '\n');
    }
}

static void two(char *out, UInt16 v)
{
    out[0] = (char)('0' + v / 10 % 10);
    out[1] = (char)('0' + v % 10);
    out[2] = 0;
}

static Boolean find_module(void)
{
    UInt16 card;
    UInt8 removable, installed;
    UInt32 base, size, ns;
    char buf[48];

    for (card = 1; card < 4; card++) {
        removable = installed = 0;
        if (HsCardAttrGet(card, hsAttrRemovable, &removable) ||
            HsCardAttrGet(card, hsAttrHwInstalled, &installed) || !removable || !installed)
            continue;
        if (HsCardAttrGet(card, hsAttrCsBase, &base) || HsCardAttrGet(card, hsAttrCsSize, &size))
            continue;
        ns = ACCESS_NS;
        HsCardAttrSet(card, hsAttrAccessTime, &ns);
        if (HsCardAttrGet(card, hsAttrAccessTime, &ns)) ns = 0;
        gBase = base;
        StrPrintF(buf, "card %u CS0 %lx size %lx %lu ns", card, base, size, ns);
        say(buf);
        return true;
    }
    say("No Springboard module found");
    return false;
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

/* One request/reply through the mailbox. Returns the SBN_RES_* result, or
 * 0xFFFF if the firmware never answered the doorbell. */
static UInt16 exchange(const UInt8 *req, UInt16 len, UInt16 *replyLen)
{
    UInt16 i, n, w, result;
    UInt32 start;

    if (++gSeq == 0) gSeq = 1;
    token(SBN_TK_BEGIN, 0);
    for (i = 0; i < len; i++) token(SBN_TK_BYTE, req[i]);
    token(SBN_TK_DOORBELL, gSeq);
    start = TimGetTicks();
    while (rd(SBN_R_RESP_SEQ) != gSeq)
        if (TimGetTicks() - start > 12 * SysTicksPerSecond()) return 0xFFFF;
    result = rd(SBN_R_RESULT);
    n = rd(SBN_R_RESP_LEN);
    if (n > sizeof gResp) n = sizeof gResp;
    for (i = 0; i < n; i += 2) {
        w = rd(SBN_R_RESP + i);
        gResp[i] = (UInt8)(w >> 8);
        if (i + 1 < n) gResp[i + 1] = (UInt8)w;
    }
    *replyLen = n;
    return result;
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
    UInt16 magic = rd(SBN_R_MAGIC), version = rd(SBN_R_VERSION), status = rd(SBN_R_STATUS);
    UInt16 t1 = rd(SBN_R_TICKS), t2;
    char buf[48];

    SysTaskDelay(SysTicksPerSecond() / 10);
    t2 = rd(SBN_R_TICKS);
    StrPrintF(buf, "id %x abi %x status %x", magic, version, status);
    say(buf);
    StrPrintF(buf, "clock %u -> %u %s", t1, t2, t1 != t2 ? "(live)" : "(STUCK)");
    say(buf);
    /* Carry on from the firmware's last reply so a stale one never matches. */
    gSeq = (UInt8)rd(SBN_R_RESP_SEQ);
}

static void echo(void)
{
    UInt16 i, k, bad = 0, before = rd(SBN_R_TOKENS);
    UInt8 v;
    char buf[48];

    for (i = 0; i < 256; i++) {
        v = (UInt8)(i * 37 + 11);
        token(SBN_TK_ECHO, v);
        for (k = 0; k < 2000 && rd(SBN_R_ECHO) != v; k++)
            ;
        if (k == 2000) bad++;
    }
    StrPrintF(buf, "echo 256: %u wrong, tokens +%u", bad, (UInt16)(rd(SBN_R_TOKENS) - before));
    say(buf);
}

static void loop(void)
{
    UInt16 n, i, len, got, ok = 0, bad = 0, result = 0;
    UInt32 t0 = TimGetTicks(), bytes = 0;
    char buf[48];

    token(SBN_TK_CONTROL, SBN_CTL_LOOPBACK_ON);
    for (n = 0; n < 40; n++) {
        len = (UInt16)(1 + (n * 37) % sizeof gReq);
        for (i = 0; i < len; i++) gReq[i] = (UInt8)(n * 7 + i * 13);
        result = exchange(gReq, len, &got);
        if (result == SBN_RES_OK && got == len && MemCmp(gReq, gResp, len) == 0) ok++;
        else bad++;
        bytes += len;
    }
    token(SBN_TK_CONTROL, SBN_CTL_LOOPBACK_OFF);
    StrPrintF(buf, "loop %u ok %u bad (last %u)", ok, bad, result);
    say(buf);
    StrPrintF(buf, "%lu bytes each way, %lu ms", bytes,
              (TimGetTicks() - t0) * 1000 / SysTicksPerSecond());
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

    result = exchange(gReq, fb_request(gReq, 0x45, 0x01), &got);
    if (result != SBN_RES_OK) {
        StrPrintF(buf, "GetTime: link result %u", result);
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

    result = exchange(gReq, fb_request(gReq, 0x45, 0x04), &got);
    status = result == SBN_RES_OK ? fb_reply(got, 0x45, 0x04, &p, &len) : 0xFF;
    if (!status && len >= 2 && p[1] < 30 && 2 + p[1] <= len) {
        StrCopy(buf, "NIO timezone ");
        MemMove(buf + 13, p + 2, p[1]);
        buf[13 + p[1]] = 0;
        say(buf);
    }
}

static void run(void (*fn)(void))
{
    char buf[40];
    Err err;

    if (!gBase && !find_module()) return;
    err = guarded(fn);
    if (err) {
        StrPrintF(buf, "Module removed (err %x)", err);
        say(buf);
        gBase = 0;
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
