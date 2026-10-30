/* Classic fujinet-lib error codes, for apps ported from fujinet-lib-palmos. */
#ifndef FUJINET_ERR_H
#define FUJINET_ERR_H
#include <stdint.h>
typedef uint8_t FN_ERR;
#define FN_ERR_OK        (0x00)
#define FN_ERR_IO_ERROR  (0x01)
#define FN_ERR_BAD_CMD   (0x02)
#define FN_ERR_OFFLINE   (0x03)
#define FN_ERR_WARNING   (0x04)
#define FN_ERR_NO_DEVICE (0x05)
#ifndef FN_ERR_UNKNOWN
#define FN_ERR_UNKNOWN   0xFF
#endif
#endif
