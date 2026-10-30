/* stdint.h for prc-tools (m68k-palmos-gcc 2.95), where int is 16 bits. */
#ifndef SBNIO_STDINT_H
#define SBNIO_STDINT_H
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef long int32_t;
typedef unsigned long uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef unsigned long uintptr_t;
typedef long intptr_t;
#define INT8_MAX 127
#define UINT8_MAX 255
#define INT16_MAX 32767
#define UINT16_MAX 65535U
#define INT32_MAX 2147483647L
#define UINT32_MAX 4294967295UL
#endif
