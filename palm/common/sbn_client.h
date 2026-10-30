/* Palm side of the springboard-nio mailbox (include/sbnio.h): find the
 * module and exchange raw FujiBus packets with fujinet-nio through it. */
#ifndef SBN_CLIENT_H
#define SBN_CLIENT_H
#include <PalmOS.h>

/* Locate the Springboard module and its CS0 window. 0 if none. */
UInt32 sbn_open(void);
UInt32 sbn_base(void);

/* Send one request and wait for its reply. Returns an SBN_RES_* value, or
 * SBN_NO_ANSWER if the firmware never published a reply. */
#define SBN_NO_ANSWER 0xFFFF
UInt16 sbn_exchange(const UInt8 *req, UInt16 len, UInt8 *resp, UInt16 cap, UInt16 *resp_len);

/* One line of text to the firmware console. */
void sbn_log(const char *s);

#endif
