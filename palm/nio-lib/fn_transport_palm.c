/* fujinet-nio-lib transport for Palm OS: raw FujiBus packets through the
 * springboard-nio CS1 link. The firmware carries them to NIO. */
#include "sbn_client.h"
#include "sbnio.h"
#include "fujinet-nio.h"
#include "fn_internal.h"
#include "fn_platform.h"

uint8_t fn_transport_init(void)
{
    return sbn_open() ? FN_OK : FN_ERR_NOT_FOUND;
}

void fn_transport_close(void) {}

uint8_t fn_transport_ready(void)
{
    return sbn_base() ? 1 : 0;
}

uint8_t fn_transport_exchange_buffers(const uint8_t *request, uint16_t request_length,
                                      uint8_t *response, uint16_t response_capacity,
                                      uint16_t *response_length)
{
    UInt16 got = 0, result;

    if (!response_length || !request || !response) return FN_ERR_INVALID;
    result = sbn_exchange(request, request_length, response, response_capacity, &got);
    *response_length = got;
    switch (result) {
    case SBN_RES_OK: return FN_OK;
    case SBN_RES_NO_HOST: return FN_ERR_TRANSPORT;
    case SBN_RES_TIMEOUT:
    case SBN_RES_UNANSWERED:
    case SBN_NO_ANSWER: return FN_ERR_TIMEOUT;
    case SBN_RES_BAD_LEN: return FN_ERR_INVALID;
    case SBN_RES_ABORTED: return FN_ERR_ABORTED;
    case SBN_RES_BAD_CHECK:
    case SBN_BAD_READ:
    default: return FN_ERR_IO;
    }
}

uint8_t fn_transport_exchange(void)
{
    return fn_transport_exchange_buffers(_fn_transport_ctx.request, _fn_transport_ctx.req_len,
                                         _fn_transport_ctx.response, _fn_transport_ctx.resp_max,
                                         &_fn_transport_ctx.resp_len);
}

const char *fn_platform_name(void)
{
    return "palmos-springboard";
}
