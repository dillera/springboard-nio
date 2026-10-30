#include "sbn_client.h"
#include "sbnio.h"

/* Handspring card attributes (HsExt.h) through the HsSelector trap. */
Err HsCardAttrGet(UInt16 cardNo, UInt16 attr, void *valueP)
    __attribute__((__callseq__("move.w #6,-(%%sp); trap #15; dc.w 0xA349")));

enum { hsAttrRemovable = 0, hsAttrHwInstalled = 1, hsAttrCsBase = 3, hsAttrCsSize = 4 };

/* NIO's own HTTP opens can take a while (DNS, TLS); the firmware gives up
 * after 30 s and publishes a timeout, so wait a little longer than that. */
#define REPLY_WAIT_SECONDS 35

/* A request the firmware found damaged is sent again; a reply that fails its
 * check is read again (it stays in place) before giving up. */
#define SEND_TRIES 3
#define READ_RETRIES 3

static UInt32 gCs0, gCs1;
static UInt16 gCard;
static UInt8 gSeq;

static UInt16 rd(UInt16 off) { return *(volatile UInt16 *)(gCs1 + off); }
static void wr(UInt16 reg, UInt16 value) { *(volatile UInt16 *)(gCs1 + SBN_W(reg)) = value; }

UInt32 sbn_open(void)
{
    UInt16 card;
    UInt8 removable, installed;
    UInt32 base, size;

    if (gCs1) return gCs1;
    for (card = 1; card < 4; card++) {
        removable = installed = 0;
        if (HsCardAttrGet(card, hsAttrRemovable, &removable) ||
            HsCardAttrGet(card, hsAttrHwInstalled, &installed) || !removable || !installed ||
            HsCardAttrGet(card, hsAttrCsBase, &base) || HsCardAttrGet(card, hsAttrCsSize, &size))
            continue;
        /* CS1 sits right above the card's CS0 window. */
        gCs1 = base + size;
        if (rd(SBN_R_MAGIC) != SBN_MAGIC_VALUE || rd(SBN_R_VERSION) != SBN_ABI_VERSION) {
            gCs1 = 0;
            continue;
        }
        gCs0 = base;
        gCard = card;
        /* Carry on from the firmware's last reply so a stale one never matches. */
        gSeq = (UInt8)rd(SBN_R_RESP_SEQ);
        return gCs1;
    }
    return 0;
}

UInt32 sbn_base(void) { return gCs1; }
UInt32 sbn_cs0(void) { return gCs0; }
UInt16 sbn_card(void) { return gCard; }
void sbn_close(void) { gCs0 = gCs1 = 0; gCard = 0; }

UInt16 sbn_read(UInt16 offset) { return gCs1 ? rd(offset) : 0; }
void sbn_write(UInt16 reg, UInt16 value) { if (gCs1) wr(reg, value); }

/* BEGIN, the bytes two per DATA word (first one high), then CHECK over them. */
static void send_request(const UInt8 *req, UInt16 len)
{
    volatile UInt16 *port = (volatile UInt16 *)(gCs1 + SBN_W(SBN_W_DATA));
    UInt16 pairs = len >> 1, w, check = 0;

    wr(SBN_W_BEGIN, len);
    while (pairs--) {
        w = (UInt16)((UInt16)req[0] << 8 | req[1]);
        req += 2;
        check = SBN_CHECK_STEP(check, w);
        *port = w;
    }
    if (len & 1) {
        w = (UInt16)((UInt16)req[0] << 8);
        check = SBN_CHECK_STEP(check, w);
        *port = w;
    }
    wr(SBN_W_CHECK, check);
}

/* Read the reply words, keep at most cap bytes, check every word. False if
 * the length or the check did not read back right. */
static Boolean read_reply(UInt8 *resp, UInt16 cap, UInt16 *resp_len)
{
    volatile UInt16 *port = (volatile UInt16 *)(gCs1 + SBN_R_RESP);
    UInt16 n = rd(SBN_R_RESP_LEN), want = rd(SBN_R_RESP_CHECK);
    UInt16 keep, pairs, words, w, check = 0;

    if (n > SBN_RESP_MAX) return false;
    keep = n < cap ? n : cap;
    pairs = keep >> 1;
    words = ((n + 1) >> 1) - pairs;
    while (pairs--) {
        w = *port++;
        check = SBN_CHECK_STEP(check, w);
        resp[0] = (UInt8)(w >> 8);
        resp[1] = (UInt8)w;
        resp += 2;
    }
    if (words) {
        /* The word with an odd last byte to keep, or the first one past cap. */
        w = *port++;
        check = SBN_CHECK_STEP(check, w);
        if (keep & 1) *resp = (UInt8)(w >> 8);
        words--;
    }
    while (words--) {
        w = *port++;
        check = SBN_CHECK_STEP(check, w);
    }
    *resp_len = keep;
    return check == want;
}

UInt16 sbn_exchange(const UInt8 *req, UInt16 len, UInt8 *resp, UInt16 cap, UInt16 *resp_len)
{
    UInt16 tries, result = SBN_RES_BAD_CHECK;
    UInt32 start, wait = (UInt32)REPLY_WAIT_SECONDS * SysTicksPerSecond();

    *resp_len = 0;
    if (!sbn_open()) return SBN_NO_ANSWER;
    if (!len || len > SBN_REQ_MAX) return SBN_RES_BAD_LEN;
    for (tries = 0; tries < SEND_TRIES && result == SBN_RES_BAD_CHECK; tries++) {
        if (++gSeq == 0) gSeq = 1;
        send_request(req, len);
        wr(SBN_W_DOORBELL, gSeq);
        start = TimGetTicks();
        while (rd(SBN_R_RESP_SEQ) != gSeq)
            if (TimGetTicks() - start > wait) return SBN_NO_ANSWER;
        result = rd(SBN_R_RESULT);
    }
    /* Only a good result has a reply to read. */
    if (result != SBN_RES_OK) return result;
    for (tries = 0; tries <= READ_RETRIES; tries++)
        if (read_reply(resp, cap, resp_len)) return result;
    *resp_len = 0;
    return SBN_BAD_READ;
}

/* Two characters per LOG word, high byte first; the newline rides in the
 * last word and a 0 character means none. */
void sbn_log(const char *s)
{
    UInt16 hi, lo;

    if (!gCs1) return;
    for (;;) {
        hi = (UInt8)*s;
        if (!hi) {
            wr(SBN_W_LOG, (UInt16)('\n' << 8));
            return;
        }
        lo = (UInt8)*++s;
        if (!lo) {
            wr(SBN_W_LOG, (UInt16)(hi << 8 | '\n'));
            return;
        }
        s++;
        wr(SBN_W_LOG, (UInt16)(hi << 8 | lo));
    }
}
