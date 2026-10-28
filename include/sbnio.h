/* springboard-nio mailbox ABI, shared by the RP2350 firmware and Palm code.
 *
 * Everything runs over CS0, so a module only needs the ROM select. The ROM
 * image may use offsets 0x0000-0x7FFF; the rest of the first 64 KiB of the CS0
 * window (HsCardAttrGet CsBase) belongs to the mailbox:
 *
 *   0x8000-0xBFFF  registers and the reply, read with MOVE.W
 *   0xC000-0xFFFF  tokens: the Palm sends a byte by READING
 *                  SBN_TOKEN(type, byte); the firmware logs which address
 *                  was read. The value read back is meaningless.
 *
 * Nothing is ever written to the bus. A request is one raw FujiBus packet sent
 * as SBN_TK_BEGIN, then one SBN_TK_BYTE per byte, then SBN_TK_DOORBELL with a
 * sequence 1-255. The reply is the raw packet NIO sent back; the firmware adds
 * and strips the SLIP framing.
 */
#ifndef SBNIO_H
#define SBNIO_H

#define SBN_MAGIC_VALUE   0x464E   /* "FN" */
#define SBN_ABI_VERSION   0x0002

/* Registers, as byte offsets from the CS0 base */
#define SBN_REGS          0x8000
#define SBN_R_MAGIC       0x8000
#define SBN_R_VERSION     0x8002
#define SBN_R_STATUS      0x8004
#define SBN_R_ACK_SEQ     0x8006   /* last doorbell sequence received */
#define SBN_R_RESP_SEQ    0x8008   /* sequence of the reply now in RESP */
#define SBN_R_RESP_LEN    0x800A   /* reply length in bytes */
#define SBN_R_RESULT      0x800C   /* SBN_RES_* for RESP_SEQ */
#define SBN_R_TOKENS      0x800E   /* tokens seen, low 16 bits */
#define SBN_R_ECHO        0x8010   /* last SBN_TK_ECHO byte */
#define SBN_R_REQ_BYTES   0x8012   /* request bytes since the last BEGIN */
#define SBN_R_TICKS       0x8014   /* firmware milliseconds, low 16 bits */
#define SBN_R_RESP        0x9000   /* reply bytes, two per word, first byte in D15-D8 */
#define SBN_RESP_MAX      4096
#define SBN_REQ_MAX       4096

/* SBN_R_STATUS bits */
#define SBN_ST_HOST       0x0001   /* NIO has the USB link port open */
#define SBN_ST_BUSY       0x0002   /* a request is waiting for its reply */
#define SBN_ST_LOOPBACK   0x0004   /* requests are echoed back without NIO */

/* SBN_R_RESULT values */
#define SBN_RES_OK        0
#define SBN_RES_NO_HOST   1        /* NIO is not running */
#define SBN_RES_TIMEOUT   2        /* NIO sent nothing back */
#define SBN_RES_BAD_LEN   3        /* request empty or over SBN_REQ_MAX */
#define SBN_RES_TOO_BIG   5        /* reply longer than SBN_RESP_MAX */
#define SBN_RES_ABORTED   6

/* Tokens: type in A9-A13, byte in A1-A8 */
#define SBN_TOKENS        0xC000
#define SBN_TOKEN(type, byte) (SBN_TOKENS | (type) << 9 | ((byte) & 0xFF) << 1)
#define SBN_TK_BEGIN      0        /* start a new request */
#define SBN_TK_BYTE       1        /* append one request byte */
#define SBN_TK_DOORBELL   2        /* byte = sequence: send the request */
#define SBN_TK_CONTROL    3        /* byte = SBN_CTL_* */
#define SBN_TK_LOG        4        /* one character for the firmware console */
#define SBN_TK_ECHO       5        /* byte copied to SBN_R_ECHO */

#define SBN_CTL_ABORT        1
#define SBN_CTL_LOOPBACK_ON  2
#define SBN_CTL_LOOPBACK_OFF 3

#endif
