#ifndef _MDX_STORAGE_H_
#define _MDX_STORAGE_H_

/*
 * mdx_storage.h - SRAM access layer (SPEC 8.2 "must not assume SRAM present").
 *
 * Thin, byte-exclusive wrapper over SGDK's SRAM_readByte / SRAM_writeByte.
 *
 *   - MdxStorage_Init() enables the mapper SRAM window (SRAM_enable()) and runs
 *     a presence self-test: it writes two distinct sentinel bytes to two
 *     distinct SRAM offsets, reads them back, and only then declares SRAM
 *     available. If a read-back mismatches (no writable SRAM, or ROM-mapped /
 *     write-protected), g_sramAvailable stays FALSE and every later write is a
 *     no-op while ReadByte returns 0 - the app stays usable on carts/regions
 *     without backup RAM. The self-test offsets are restored afterwards so an
 *     existing save is never clobbered.
 *
 *   - Access is ALWAYS byte-wise and ALWAYS relative to the vendored SRAM_BASE
 *     (0x200001, odd). We never call SRAM_readWord/readLong/writeWord/
 *     writeLong (word/long on an odd base raises a 68000 address error on real
 *     hardware), and we never change SRAM_BASE to even 0x200000.
 *
 *   - When SRAM is unavailable the wrappers touch NO memory at all (no ROM
 *     dereference, no crash) - the guards short-circuit before any SRAM access.
 */

#include <genesis.h>


/* Enable the SRAM window and run the presence self-test. Call once at boot. */
void MdxStorage_Init(void);

/* Byte-wise read/write over SRAM_readByte / SRAM_writeByte only. */
u8  MdxStorage_ReadByte(u32 offset);
void MdxStorage_WriteByte(u32 offset, u8 value);

/* Result of the boot-time presence self-test (FALSE when no writable SRAM). */
bool MdxStorage_IsAvailable(void);


#endif /* _MDX_STORAGE_H_ */
