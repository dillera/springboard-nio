#include "sbn_client.h"
#include "sbnio.h"

/* Handspring card attributes (HsExt.h) through the HsSelector trap. */
Err HsCardAttrGet(UInt16 cardNo, UInt16 attr, void *valueP)
    __attribute__((__callseq__("move.w #6,-(%%sp); trap #15; dc.w 0xA349")));

enum { hsAttrRemovable = 0, hsAttrHwInstalled = 1, hsAttrCsBase = 3 };

/* NIO's own HTTP opens can take a while (DNS, TLS); the firmware gives up
 * after 30 s and publishes a timeout, so wait a little longer than that. */
#define REPLY_WAIT_SECONDS 35

static UInt32 gBase;
static UInt8 gSeq;
static volatile UInt16 gSink;

static UInt16 rd(UInt16 off) { return *(volatile UInt16 *)(gBase + off); }

/* Send one byte by reading the token address that encodes it. */
static void token(UInt16 type, UInt8 byte)
{
    gSink = *(volatile UInt16 *)(gBase + (UInt32)SBN_TOKEN(type, byte));
}

UInt32 sbn_open(void)
{
    UInt16 card;
    UInt8 removable, installed;
    UInt32 base;

    if (gBase) return gBase;
    for (card = 1; card < 4; card++) {
        removable = installed = 0;
        if (HsCardAttrGet(card, hsAttrRemovable, &removable) ||
            HsCardAttrGet(card, hsAttrHwInstalled, &installed) || !removable || !installed ||
            HsCardAttrGet(card, hsAttrCsBase, &base))
            continue;
        if (*(volatile UInt16 *)(base + SBN_R_MAGIC) != SBN_MAGIC_VALUE) continue;
        gBase = base;
        /* Carry on from the firmware's last reply so a stale one never matches. */
        gSeq = (UInt8)rd(SBN_R_RESP_SEQ);
        return gBase;
    }
    return 0;
}

UInt32 sbn_base(void) { return gBase; }

UInt16 sbn_exchange(const UInt8 *req, UInt16 len, UInt8 *resp, UInt16 cap, UInt16 *resp_len)
{
    UInt16 i, n, w;
    UInt32 start;

    *resp_len = 0;
    if (!gBase && !sbn_open()) return SBN_NO_ANSWER;
    if (++gSeq == 0) gSeq = 1;
    token(SBN_TK_BEGIN, 0);
    for (i = 0; i < len; i++) token(SBN_TK_BYTE, req[i]);
    token(SBN_TK_DOORBELL, gSeq);
    start = TimGetTicks();
    while (rd(SBN_R_RESP_SEQ) != gSeq)
        if (TimGetTicks() - start > REPLY_WAIT_SECONDS * SysTicksPerSecond()) return SBN_NO_ANSWER;
    n = rd(SBN_R_RESP_LEN);
    if (n > cap) n = cap;
    for (i = 0; i < n; i += 2) {
        w = rd(SBN_R_RESP + i);
        resp[i] = (UInt8)(w >> 8);
        if (i + 1 < n) resp[i + 1] = (UInt8)w;
    }
    *resp_len = n;
    return rd(SBN_R_RESULT);
}

void sbn_log(const char *s)
{
    if (!gBase) return;
    for (; *s; s++) token(SBN_TK_LOG, (UInt8)*s);
    token(SBN_TK_LOG, '\n');
}
