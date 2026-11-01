#ifndef _MDX_PATCH_FORMAT_H_
#define _MDX_PATCH_FORMAT_H_

/*
 * mdx_patch_format.h - SRAM patch persistence format (SPEC 8.1 / 8.3).
 *
 * An EXPLICIT, big-endian, byte-serialised layout (NO memcpy(struct) or implicit
 * packing) so the on-disk blob is stable across compilers and struct layout.
 *
 * Blob layout (all integers big-endian; u16 = high byte first):
 *
 *   offset 0  : formatVersion      (u16)   = MDX4_PATCH_FORMAT_VERSION
 *   offset 2  : patchCount         (u16)
 *   offset 4  : patch[0]           (PATCH_RECORD_SIZE bytes)
 *               ...
 *               patch[count-1]
 *   checksumOff = 4 + count*PATCH_RECORD_SIZE
 *              : checksum           (u16)   16-bit additive over [0 .. checksumOff-1]
 *   markerOff  = checksumOff + 2
 *              : magic[4]           "MDX4"  (0x4D 0x44 0x58 0x34)
 *              : trailVersion       (u16)   = MDX4_PATCH_FORMAT_VERSION
 *
 * CRITICAL: the magic + trailVersion "valid" marker is written LAST (after the
 * checksum). On a half-written blob (power loss mid-save, or garbage) the marker
 * is either absent or wrong, and the checksum over the leading bytes will not
 * match - so a truncated / corrupt SRAM region is detected and rejected instead
 * of being trusted. On load we verify BOTH the trailing marker AND the checksum
 * before reading a single patch field.
 *
 *   PATCH_RECORD_SIZE = 16 (name) + 6 (global) + 4*11 (operators) = 66 bytes.
 *
 * Return codes: a Save/Load returns an MdxPatchFormatResult. Success is >= 0
 * (the loaded patch count); failure is a negative code. On any failure the
 * caller must NOT trust the output buffer and should fall back to defaults.
 */

#include <genesis.h>

#include "core/mdx_patch.h"     /* FMPatch, FMOperator */


/* Format version stamped into every blob. Bump on incompatible layout change. */
#define MDX4_PATCH_FORMAT_VERSION   1

/* Magic "MDX4" written as the trailing validity marker (4 bytes). */
#define MDX4_PATCH_MAGIC_0  0x4D   /* 'M' */
#define MDX4_PATCH_MAGIC_1  0x44   /* 'D' */
#define MDX4_PATCH_MAGIC_2  0x58   /* 'X' */
#define MDX4_PATCH_MAGIC_3  0x34   /* '4' */

/* Hard ceiling on patches in one blob (bounds marker/checksum offsets). */
#define MDX4_MAX_PATCHES    64

/* Bytes per serialised patch record. */
#define MDX4_PATCH_RECORD_SIZE  66

/* Name field width (matches FMPatch.name[16]). */
#define MDX4_PATCH_NAME_LEN     16


/* Result / error codes. Success >= 0; failure < 0. */
typedef enum
{
    MDX_PFMT_OK       =  0,   /* load: success (value = patch count)            */
    MDX_PFMT_ERR_NOSRAM   = -1, /* SRAM not present / not writable             */
    MDX_PFMT_ERR_VERSION  = -2, /* format/trailing version mismatch            */
    MDX_PFMT_ERR_COUNT    = -3, /* patch count out of range / unsupported      */
    MDX_PFMT_ERR_CHECKSUM = -4  /* missing / corrupt marker or checksum        */
} MdxPatchFormatResult;


/* Total serialised size (bytes) of a blob holding `count` patches, INCLUDING
 * the trailing marker. Used by callers to sanity-check available SRAM. */
u32 MdxPatchFormat_BlobSize(u16 count);


/*
 * Serialise `count` patches to SRAM starting at `baseOffset`.
 *
 * Writes header + patches + checksum, then the trailing magic/version marker
 * LAST. Returns MDX_PFMT_OK on success, or a negative error code (no-SRAM /
 * bad count). On no-SRAM it performs NO write.
 */
int MdxPatchFormat_Save(const FMPatch* patches, u16 count, u32 baseOffset);


/*
 * Deserialise up to `maxCount` patches from SRAM at `baseOffset` into `patches`.
 *
 * Verifies the trailing magic/version marker AND the additive checksum BEFORE
 * reading any patch field. Returns the patch count (>= 0) on success, or a
 * negative error code on any validation failure. On failure `*patches` is left
 * untouched (caller should load defaults). Each loaded patch is clamped to
 * legal YM2612 ranges before it is returned.
 */
int MdxPatchFormat_Load(FMPatch* patches, u16 maxCount, u32 baseOffset);


#endif /* _MDX_PATCH_FORMAT_H_ */
