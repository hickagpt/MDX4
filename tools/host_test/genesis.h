/*
 * genesis.h - MINIMAL host build shim for the MDX-4 register-trace harness.
 *
 * This is NOT the SGDK header. It provides ONLY the fixed-width integer types
 * and TRUE/FALSE that the pure-C synth modules (mdx_patch / mdx_operator /
 * mdx_algorithm / mdx_envelope / mdx_lfo / mdx_frequency / mdx_synth) and the
 * driver header (mdx_ym2612.h) need to compile on the host (Windows/clang).
 *
 * The real Genesis hardware port writes ($A04000.., PSG $C00011) are replaced
 * by an in-memory capture buffer in ym2612_host.c, so no Genesis memory is
 * touched. The ENGINE under test (mdx_synth.c) is compiled REAL, unmodified.
 */
#ifndef _GENESIS_HOST_SHIM_H_
#define _GENESIS_HOST_SHIM_H_

#include <stdint.h>
#include <stdbool.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef uint64_t u64;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#endif /* _GENESIS_HOST_SHIM_H_ */
