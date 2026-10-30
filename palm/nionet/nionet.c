/* NIO Net: real network requests from a Visor through fujinet-nio-lib, over
 * the Springboard mailbox to fujinet-nio on the host.
 *   HTTP / HTTPS  GET a page, show the status, size and first line
 *   ISS           NIO JSON translation: the station's position by JSON Pointer
 * Each line shown also goes to the RP2350 console. */
#include <PalmOS.h>
#include "nionet_rsc.h"
#include "fujinet-nio.h"
#include "sbn_client.h"

#define LINES 11
#define LINE_CHARS 40

static char gLines[LINES][LINE_CHARS];
static UInt16 gLineCount;
static Boolean gReady;
static uint8_t gBuf[512];

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
    sbn_log(s);
}

static Boolean ready(void)
{
    char buf[40];
    uint8_t err;

    if (gReady) return true;
    err = fn_init();
    if (err) {
        StrPrintF(buf, "fn_init failed: %u", err);
        say(buf);
        return false;
    }
    gReady = true;
    return true;
}

static UInt32 ms_since(UInt32 t0)
{
    return (TimGetTicks() - t0) * 1000 / SysTicksPerSecond();
}

/* Copy the first printable line of the body into out. */
static void first_line(const uint8_t *p, uint16_t n, char *out, UInt16 max)
{
    UInt16 i, k = 0;
    for (i = 0; i < n && p[i] != '\n' && k < max - 1; i++)
        if (p[i] >= 32 && p[i] < 127) out[k++] = (char)p[i];
    out[k] = 0;
}

static void get(const char *url)
{
    fn_handle_t h;
    uint8_t err, flags = 0, iflags = 0;
    uint16_t status = 0, got;
    uint32_t length = 0, total = 0;
    UInt16 tries;
    UInt32 t0 = TimGetTicks();
    char buf[48], line[LINE_CHARS];

    if (!ready()) return;
    say(url);
    err = fn_open(&h, FN_METHOD_GET, url, FN_OPEN_FOLLOW_REDIR);
    if (err) {
        StrPrintF(buf, "open failed: %u", err);
        say(buf);
        return;
    }
    for (tries = 0; tries < 50; tries++) {
        err = fn_info(h, &status, &length, &iflags);
        if (err != FN_ERR_NOT_READY) break;
        SysTaskDelay(SysTicksPerSecond() / 10);
    }
    StrPrintF(buf, "HTTP %u, length %lu (info %u)", status, length, err);
    say(buf);
    line[0] = 0;
    for (tries = 0; tries < 200;) {
        got = 0;
        err = fn_read(h, total, gBuf, sizeof gBuf, &got, &flags);
        if (err == FN_ERR_NOT_READY) {
            tries++;
            SysTaskDelay(SysTicksPerSecond() / 20);
            continue;
        }
        if (err) {
            StrPrintF(buf, "read failed at %lu: %u", total, err);
            say(buf);
            break;
        }
        if (!total && got) first_line(gBuf, got, line, sizeof line);
        total += got;
        if ((flags & FN_READ_EOF) || (!got && length && total >= length)) break;
    }
    fn_close(h);
    StrPrintF(buf, "read %lu bytes in %lu ms", total, ms_since(t0));
    say(buf);
    if (line[0]) say(line);
}

/* NIO fetches the URL, parses the JSON and returns just the selected value. */
static Boolean json_value(const char *url, const char *pointer, char *out, UInt16 max)
{
    fn_handle_t h;
    uint8_t err, flags = 0;
    uint16_t got = 0;
    UInt16 tries;
    char buf[40];

    out[0] = 0;
    err = fn_open_translated(&h, FN_METHOD_GET, url, 0, FN_TRANSLATE_JSON, 0, pointer);
    if (err) {
        StrPrintF(buf, "open %s failed: %u", pointer, err);
        say(buf);
        return false;
    }
    for (tries = 0; tries < 100; tries++) {
        err = fn_read(h, 0, gBuf, max - 1, &got, &flags);
        if (err != FN_ERR_NOT_READY) break;
        SysTaskDelay(SysTicksPerSecond() / 20);
    }
    fn_close(h);
    if (err) {
        StrPrintF(buf, "read %s failed: %u", pointer, err);
        say(buf);
        return false;
    }
    MemMove(out, gBuf, got);
    out[got] = 0;
    return true;
}

static void iss(void)
{
    static const char url[] = "http://api.open-notify.org/iss-now.json";
    char lat[24], lon[24], buf[48];
    UInt32 t0 = TimGetTicks();

    if (!ready()) return;
    say("ISS position (open-notify)");
    if (!json_value(url, "/iss_position/latitude", lat, sizeof lat)) return;
    if (!json_value(url, "/iss_position/longitude", lon, sizeof lon)) return;
    StrPrintF(buf, "lat %s lon %s", lat, lon);
    say(buf);
    StrPrintF(buf, "two JSON queries in %lu ms", ms_since(t0));
    say(buf);
}

static Boolean MainFormHandleEvent(EventType *e)
{
    if (e->eType == frmOpenEvent) {
        FrmDrawForm(FrmGetActiveForm());
        say(sbn_open() ? "Springboard NIO module found" : "No Springboard NIO module");
        return true;
    }
    if (e->eType == ctlSelectEvent) {
        switch (e->data.ctlSelect.controlID) {
        case BtnHttp: get("http://example.com/"); return true;
        case BtnHttps: get("https://example.com/"); return true;
        case BtnIss: iss(); return true;
        case BtnClear: gLineCount = 0; draw(); return true;
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
    if (gReady) fn_shutdown();
    return 0;
}
