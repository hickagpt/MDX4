/*
 * mdx_patch_format.c - SRAM patch persistence format (SPEC 8.1 / 8.3).
 *
 * See mdx_patch_format.h for the byte-exact layout. All SRAM traffic goes
 * through MdxStorage_ReadByte / MdxStorage_WriteByte (byte-wise, odd-base only,
 * no-SRAM-guarded), so a missing/protected SRAM simply yields an error code and
 * never touches ROM space.
 */

#include "mdx_patch_format.h"

#include <genesis.h>

#include "storage/mdx_storage.h"
#include "core/mdx_patch.h"


/* --- layout offsets (relative to baseOffset). --- */
#define PF_OFF_FORMAT_VERSION  0   /* u16 */
#define PF_OFF_PATCH_COUNT     2   /* u16 */
#define PF_OFF_RECORDS         4   /* patch[0] starts here */


u32 MdxPatchFormat_BlobSize(u16 count)
{
    /* 4 (version+count) + count*record + 2 (checksum) + 6 (magic+version). */
    return (u32)4 + (u32)count * MDX4_PATCH_RECORD_SIZE + (u32)2 + (u32)6;
}


/* --- byte put/get helpers (big-endian). --- */
static void pf_put_u16(u32 base, u32 off, u16 v)
{
    MdxStorage_WriteByte(base + off,     (u8)((v >> 8) & 0xFF));
    MdxStorage_WriteByte(base + off + 1, (u8)( v       & 0xFF));
}

static u16 pf_get_u16(u32 base, u32 off)
{
    const u16 hi = (u16)MdxStorage_ReadByte(base + off);
    const u16 lo = (u16)MdxStorage_ReadByte(base + off + 1);
    return (u16)((hi << 8) | lo);
}

static void pf_put_u8(u32 base, u32 off, u8 v)
{
    MdxStorage_WriteByte(base + off, v);
}

static u8 pf_get_u8(u32 base, u32 off)
{
    return MdxStorage_ReadByte(base + off);
}


/* Serialise one patch record; returns the number of bytes written (record size). */
static u32 pf_put_patch(u32 base, u32 off, const FMPatch* p)
{
    u32 o = off;
    u8  i;

    /* name[16] - copy raw (caller keeps it NUL-terminated in FMPatch). */
    for (i = 0; i < MDX4_PATCH_NAME_LEN; i++)
        pf_put_u8(base, o + i, (u8)p->name[i]);

    o += MDX4_PATCH_NAME_LEN;

    /* global params (6 bytes). */
    pf_put_u8(base, o + 0, p->algorithm);
    pf_put_u8(base, o + 1, p->feedback);
    pf_put_u8(base, o + 2, p->lfoEnabled);
    pf_put_u8(base, o + 3, p->lfoFrequency);
    pf_put_u8(base, o + 4, p->lfoPitchDepth);
    pf_put_u8(base, o + 5, p->lfoAmplitudeDepth);
    o += 6;

    /* 4 operators x 11 bytes. */
    for (i = 0; i < 4; i++)
    {
        const FMOperator* op = &p->operators[i];
        pf_put_u8(base, o +  0, op->detune);
        pf_put_u8(base, o +  1, op->multiplier);
        pf_put_u8(base, o +  2, op->totalLevel);
        pf_put_u8(base, o +  3, op->keyScale);
        pf_put_u8(base, o +  4, op->attackRate);
        pf_put_u8(base, o +  5, op->decay1Rate);
        pf_put_u8(base, o +  6, op->sustainLevel);
        pf_put_u8(base, o +  7, op->decay2Rate);
        pf_put_u8(base, o +  8, op->releaseRate);
        pf_put_u8(base, o +  9, op->amplitudeModulation);
        pf_put_u8(base, o + 10, op->ssgEg);
        o += 11;
    }

    return o - off;   /* MDX4_PATCH_RECORD_SIZE */
}


/* Read one patch record; returns the number of bytes read. */
static u32 pf_get_patch(u32 base, u32 off, FMPatch* p)
{
    u32 o = off;
    u8  i;

    /* name[16] */
    for (i = 0; i < MDX4_PATCH_NAME_LEN; i++)
        p->name[i] = (char)pf_get_u8(base, o + i);
    o += MDX4_PATCH_NAME_LEN;

    p->algorithm          = pf_get_u8(base, o + 0);
    p->feedback           = pf_get_u8(base, o + 1);
    p->lfoEnabled         = pf_get_u8(base, o + 2);
    p->lfoFrequency       = pf_get_u8(base, o + 3);
    p->lfoPitchDepth      = pf_get_u8(base, o + 4);
    p->lfoAmplitudeDepth  = pf_get_u8(base, o + 5);
    o += 6;

    for (i = 0; i < 4; i++)
    {
        FMOperator* op = &p->operators[i];
        op->detune             = pf_get_u8(base, o +  0);
        op->multiplier          = pf_get_u8(base, o +  1);
        op->totalLevel          = pf_get_u8(base, o +  2);
        op->keyScale            = pf_get_u8(base, o +  3);
        op->attackRate          = pf_get_u8(base, o +  4);
        op->decay1Rate          = pf_get_u8(base, o +  5);
        op->sustainLevel        = pf_get_u8(base, o +  6);
        op->decay2Rate          = pf_get_u8(base, o +  7);
        op->releaseRate         = pf_get_u8(base, o +  8);
        op->amplitudeModulation = pf_get_u8(base, o +  9);
        op->ssgEg               = pf_get_u8(base, o + 10);
        o += 11;
    }

    return o - off;   /* MDX4_PATCH_RECORD_SIZE */
}


int MdxPatchFormat_Save(const FMPatch* patches, u16 count, u32 baseOffset)
{
    u32 off;
    u16 checksum;
    u32 markerOff;
    u16 i;

    if (!MdxStorage_IsAvailable())   return MDX_PFMT_ERR_NOSRAM;
    if (count == 0 || count > MDX4_MAX_PATCHES) return MDX_PFMT_ERR_COUNT;

    /* Header (version + count). */
    pf_put_u16(baseOffset, PF_OFF_FORMAT_VERSION, MDX4_PATCH_FORMAT_VERSION);
    pf_put_u16(baseOffset, PF_OFF_PATCH_COUNT, count);

    /* Patches. */
    off = PF_OFF_RECORDS;
    for (i = 0; i < count; i++)
    {
        pf_put_patch(baseOffset, off, &patches[i]);
        off += MDX4_PATCH_RECORD_SIZE;
    }

    /* Checksum (16-bit additive) over every byte from the start of the blob up
     * to, but not including, the checksum field itself. */
    {
        u16 sum = 0;
        u32 b;
        for (b = 0; b < off; b++)
            sum = (u16)(sum + pf_get_u8(baseOffset, b));   /* wraps mod 65536 */
        checksum = sum;
    }
    pf_put_u16(baseOffset, off, checksum);

    /* Trailing validity marker LAST (magic + version). */
    markerOff = off + 2;
    pf_put_u8(baseOffset, markerOff + 0, MDX4_PATCH_MAGIC_0);
    pf_put_u8(baseOffset, markerOff + 1, MDX4_PATCH_MAGIC_1);
    pf_put_u8(baseOffset, markerOff + 2, MDX4_PATCH_MAGIC_2);
    pf_put_u8(baseOffset, markerOff + 3, MDX4_PATCH_MAGIC_3);
    pf_put_u16(baseOffset, markerOff + 4, MDX4_PATCH_FORMAT_VERSION);

    return MDX_PFMT_OK;
}


int MdxPatchFormat_Load(FMPatch* patches, u16 maxCount, u32 baseOffset)
{
    u16 formatVersion;
    u16 patchCount;
    u16 storedChecksum;
    u16 computedChecksum;
    u32 checksumOff;
    u32 markerOff;
    u32 b;
    u16 i;

    if (!MdxStorage_IsAvailable())   return MDX_PFMT_ERR_NOSRAM;

    /* Read header. */
    formatVersion = pf_get_u16(baseOffset, PF_OFF_FORMAT_VERSION);
    patchCount    = pf_get_u16(baseOffset, PF_OFF_PATCH_COUNT);

    /* Reject an unsupported / corrupt version immediately. */
    if (formatVersion != MDX4_PATCH_FORMAT_VERSION) return MDX_PFMT_ERR_VERSION;

    /* Reject an out-of-range / unsupported count before computing offsets (this
     * is also the guard against a garbage count driving offsets off the end of
     * SRAM). */
    if (patchCount == 0 || patchCount > maxCount || patchCount > MDX4_MAX_PATCHES)
        return MDX_PFMT_ERR_COUNT;

    checksumOff = PF_OFF_RECORDS + (u32)patchCount * MDX4_PATCH_RECORD_SIZE;
    markerOff  = checksumOff + 2;

    /* Verify the trailing "valid" marker is present. */
    if (pf_get_u8(baseOffset, markerOff + 0) != MDX4_PATCH_MAGIC_0) return MDX_PFMT_ERR_CHECKSUM;
    if (pf_get_u8(baseOffset, markerOff + 1) != MDX4_PATCH_MAGIC_1) return MDX_PFMT_ERR_CHECKSUM;
    if (pf_get_u8(baseOffset, markerOff + 2) != MDX4_PATCH_MAGIC_2) return MDX_PFMT_ERR_CHECKSUM;
    if (pf_get_u8(baseOffset, markerOff + 3) != MDX4_PATCH_MAGIC_3) return MDX_PFMT_ERR_CHECKSUM;
    if (pf_get_u16(baseOffset, markerOff + 4) != MDX4_PATCH_FORMAT_VERSION) return MDX_PFMT_ERR_VERSION;

    /* Verify the additive checksum over the leading bytes. */
    computedChecksum = 0;
    for (b = 0; b < checksumOff; b++)
        computedChecksum = (u16)(computedChecksum + pf_get_u8(baseOffset, b));
    storedChecksum = pf_get_u16(baseOffset, checksumOff);
    if (computedChecksum != storedChecksum) return MDX_PFMT_ERR_CHECKSUM;

    /* Both checks passed - the blob is trustworthy. Read + clamp the patches.
     * Clamping guarantees every field is within its legal YM2612 range. */
    b = PF_OFF_RECORDS;
    for (i = 0; i < patchCount; i++)
    {
        pf_get_patch(baseOffset, b, &patches[i]);
        MdxPatch_Clamp(&patches[i]);
        b += MDX4_PATCH_RECORD_SIZE;
    }

    return (int)patchCount;
}
