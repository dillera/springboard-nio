/* Link control from fujinet-lib-palmos; here the link is the Springboard. */
#ifndef FUJINET_PALMOS_H
#define FUJINET_PALMOS_H
#include <stdbool.h>
#include <stdint.h>
void fuji_palmos_close(void);
bool fuji_palmos_is_open(void);
uint16_t fuji_palmos_last_error(void);
#endif
