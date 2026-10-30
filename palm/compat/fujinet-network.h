/* The classic fujinet-lib network calls used by the fujinet-palm apps,
 * implemented on fujinet-nio-lib by fn_classic.c. Devicespecs are "N:<url>".
 * JSON queries are JSON Pointers answered by NIO's JSON translator. */
#ifndef FUJINET_NETWORK_H
#define FUJINET_NETWORK_H
#include <stdbool.h>
#include <stdint.h>
#include "fujinet-err.h"

#define OPEN_MODE_READ          (0x04)
#define OPEN_MODE_HTTP_GET      (0x04)
#define OPEN_MODE_HTTP_GET_H    (0x0C)
#define OPEN_TRANS_NONE         (0x00)

FN_ERR network_open(const char *devicespec, uint8_t mode, uint8_t trans);
int16_t network_read(const char *devicespec, void *buf, uint16_t len);
FN_ERR network_json_parse(const char *devicespec);
int16_t network_json_query(const char *devicespec, const char *query, char *buffer);
int16_t network_json_query_n(const char *devicespec, const char *query, char *buffer, uint16_t buflen);
FN_ERR network_status(const char *devicespec, uint16_t *bw, uint8_t *c, uint8_t *err);
FN_ERR network_close(const char *devicespec);
#endif
