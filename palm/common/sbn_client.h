/* Palm side of the springboard-nio CS1 link (include/sbnio.h): find the
 * module and exchange raw FujiBus packets with fujinet-nio through it. */
#ifndef SBN_CLIENT_H
#define SBN_CLIENT_H
#include <PalmOS.h>

/* Locate the Springboard module and check its CS1 registers. Returns the CS1
 * base (also sbn_base), 0 if none. sbn_close forgets it so the next sbn_open
 * looks again, e.g. after the module was pulled. */
UInt32 sbn_open(void);
UInt32 sbn_base(void);
UInt32 sbn_cs0(void);
UInt16 sbn_card(void);
void sbn_close(void);

/* One 16-bit register read at a CS1 offset (SBN_R_*), one write to a write
 * register (SBN_W_*). 0 and nothing if the module is not open. */
UInt16 sbn_read(UInt16 offset);
void sbn_write(UInt16 reg, UInt16 value);

/* Send one request and wait for its reply. Returns an SBN_RES_* value,
 * SBN_NO_ANSWER if the firmware never published a reply, or SBN_BAD_READ if
 * the reply never read back with the right check. */
#define SBN_NO_ANSWER 0xFFFF
#define SBN_BAD_READ  0xFFFE
UInt16 sbn_exchange(const UInt8 *req, UInt16 len, UInt8 *resp, UInt16 cap, UInt16 *resp_len);

/* One line of text to the firmware console. */
void sbn_log(const char *s);

#endif
