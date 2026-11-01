/*
 * host_main.c - host-side register-trace harness for the MDX-4 synth engine.
 *
 * PURPOSE
 *   Deterministically verify the register sequences the MDX-4 synthesizer
 *   ENGINE (src/core/mdx_synth.c, compiled REAL / unmodified) emits, in place
 *   of a Genesis emulator (no BlastEm/GPGX in this environment). The YM2612
 *   driver is replaced by tools/host_test/ym2612_host.c, which captures every
 *   register write (reg, value) in order into g_cap[] / g_capN. The register
 *   DERIVATION in that shim is a line-for-line copy of src/audio/mdx_ym2612.c;
 *   only the two Genesis-only side effects (YM2612_reset, PSG $C00011) are
 *   omitted. What is tested is therefore the real engine's orchestration logic.
 *
 * VERIFIED ADJUSTMENTS TO THE TASK'S TENTATIVE VALUES
 *   (per the task's own rule: "trust the CODE and adjust the assertion to match
 *    correct behavior" - the code here is correct, confirmed against the engine
 *    source AND the YM2612 datasheet/wikis):
 *
 *   1. 0x28 key-ON for channel 1 == 0xF0 (task guessed 0xF1); key-OFF == 0x00
 *      (task guessed 0x01). YM2612 register 0x28: bits 7-4 = operator key bits,
 *      bits 2-0 = CHANNEL FIELD, and that field encodes channel 1 as 000 (not
 *      001). Verified against Megadrive wiki, Plutiedev, Maxim/smspower,
 *      railgun wiki, and the ares emulator (io.cpp: field 0 -> first channel;
 *      code198x: "writing F0h starts channel 1 and 00h releases it"). The
 *      engine passes a 1-based channel (1); the driver maps it to the field via
 *      (channel-1)=0, so all-4-operators key-on = 0xF0 and key-off = 0x00.
 *      Both are CORRECT - the assertions below expect the verified values.
 *
 *   2. Frequency ordering: 0xA4 (high) is written BEFORE 0xA0 (low) - the
 *      YM2612 "high byte first" rule and the driver's own comment. The task's
 *      wording "A4 after A0" is a typo; the parenthetical "0xA4-before-0xA0"
 *      and the code agree. We assert last_index(0xA4) < last_index(0xA0).
 *
 *   3. TL / SSG-EG writes are spread across the per-operator slot offsets
 *      (0x40/0x44/0x48/0x4C and 0x90/0x94/0x98/0x9C), so "one write per
 *      operator" is counted as a register GROUP ((reg & 0xF0) == base), not as
 *      four writes to the single byte 0x40.
 *
 * Exit code = number of failed checks (0 = all pass).
 */
#include <stdio.h>
#include <stdarg.h>

#include "mdx_synth.h"     /* engine API; pulls in genesis types + patch + ym2612 macros */
#include "mdx_ym2612.h"    /* MDX_REG_*, MDX_OP_OFFSET, driver API */
#include "mdx_patch.h"     /* FMOperator, DEFAULT_PATCH */


/* Capture buffer lives in ym2612_host.c; mirror the (layout-identical) struct
 * here so we can index it. */
typedef struct { u8 reg; u8 val; } RegWrite;
extern RegWrite g_cap[];
extern int      g_capN;


static int g_fail = 0;
static int g_ok   = 0;


/* Print "ok: ..." or "FAIL: ..." and update the counters. */
static void checkf(int cond, const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    if (cond) { printf("ok:   %s\n", buf); g_ok++; }
    else      { printf("FAIL: %s\n", buf); g_fail++; }
}


/* ---- capture accessors over g_cap / g_capN ---- */
static void cap_clear(void) { g_capN = 0; }

/* Count writes to an exact register. */
static int cap_count(u8 reg)
{
    int i, n = 0;
    for (i = 0; i < g_capN; i++) if (g_cap[i].reg == reg) n++;
    return n;
}

/* Count writes to a per-operator register GROUP: (reg & 0xF0) == (base & 0xF0).
 * e.g. TL writes land on 0x40/0x44/0x48/0x4C (S1/S3/S2/S4 offsets). */
static int cap_count_group(u8 base)
{
    int i, n = 0;
    base &= 0xF0;
    for (i = 0; i < g_capN; i++) if ((g_cap[i].reg & 0xF0) == base) n++;
    return n;
}

/* Value of the last write to reg, or -1 if reg was never written. */
static s32 cap_last_val(u8 reg)
{
    int i;
    for (i = g_capN - 1; i >= 0; i--)
        if (g_cap[i].reg == reg) return (s32)g_cap[i].val;
    return -1;
}

/* Index of the last write to reg, or -1 if never written. */
static int cap_last_idx(u8 reg)
{
    int i;
    for (i = g_capN - 1; i >= 0; i--)
        if (g_cap[i].reg == reg) return i;
    return -1;
}


/* ---- Scenario 1: Init ---- */
static void scenario_init(void)
{
    printf("\n=== Scenario 1: MdxSynth_Init ===\n");
    MdxSynth_Init();

    /* 0xB4: MdxYm2612_Init writes 0xC0, then MdxLfo_Apply rewrites
     * 0xC0|(ams<<4)|pms = 0xC0 for the default patch (ams=pms=0). Last = 0xC0. */
    checkf(cap_last_val(MDX_REG_B4) == 0xC0,
           "last 0xB4 == 0xC0 (L/R enable) (got 0x%02X)",
           (unsigned)cap_last_val(MDX_REG_B4));

    /* TL: one write per operator (group 0x40). */
    checkf(cap_count_group(MDX_REG_TL) >= 4,
           ">=4 TL writes (one per operator) (got %d)",
           cap_count_group(MDX_REG_TL));

    /* SSG-EG off: 4 from MdxYm2612_Init + 4 from apply_all (group 0x90). */
    checkf(cap_count_group(MDX_REG_SSG_EG) >= 4,
           ">=4 SSG-EG writes (got %d)",
           cap_count_group(MDX_REG_SSG_EG));
}


/* ---- Scenario 2: NoteOn(69) = A4 ---- */
static void scenario_noteon_69(void)
{
    printf("\n=== Scenario 2: NoteOn(69) (A4) ===\n");
    MdxSynth_NoteOff();      /* reset key state */
    cap_clear();
    MdxSynth_NoteOn(69);

    /* 0x28 key-on: opBits 0xF0 | channel-1 field 0x00 = 0xF0 (header note 1). */
    checkf(cap_last_val(MDX_REG_KEYON) == 0xF0,
           "last 0x28 == 0xF0 (opBits 0xF0 | ch1 field 0x00) (got 0x%02X)",
           (unsigned)cap_last_val(MDX_REG_KEYON));

    /* 0xA4 = (block4 << 3) | (928 >> 8 = 3) = 0x23. */
    checkf(cap_last_val(MDX_REG_A4) == 0x23,
           "last 0xA4 == 0x23 (block4<<3 | fnumHi 3) (got 0x%02X)",
           (unsigned)cap_last_val(MDX_REG_A4));

    /* 0xA4 (high) written BEFORE 0xA0 (low) - header note 2. */
    checkf(cap_last_idx(MDX_REG_A4) >= 0 && cap_last_idx(MDX_REG_A0) >= 0 &&
           cap_last_idx(MDX_REG_A4) < cap_last_idx(MDX_REG_A0),
           "0xA4 written before 0xA0 (idx A4=%d < idx A0=%d)",
           cap_last_idx(MDX_REG_A4), cap_last_idx(MDX_REG_A0));
}


/* ---- Scenario 3: NoteOff ---- */
static void scenario_noteoff(void)
{
    printf("\n=== Scenario 3: NoteOff ===\n");
    cap_clear();
    MdxSynth_NoteOff();

    /* 0x28 key-off: opBits 0 | channel-1 field 0x00 = 0x00 (header note 1). */
    checkf(cap_last_val(MDX_REG_KEYON) == 0x00,
           "last 0x28 == 0x00 (opBits 0 | ch1 field 0x00) (got 0x%02X)",
           (unsigned)cap_last_val(MDX_REG_KEYON));
}


/* ---- Scenario 4: Live-edit operator under key-on (no retrigger) ---- */
static void scenario_live_edit(void)
{
    printf("\n=== Scenario 4: Live-edit operator (no retrigger) ===\n");
    MdxSynth_NoteOff();
    cap_clear();
    MdxSynth_NoteOn(69);          /* note on -> g_keyState = ON */
    cap_clear();                  /* isolate: capture only the edit's writes */

    FMOperator op = DEFAULT_PATCH.operators[0];   /* copy default op0 (S1) */
    op.totalLevel = 60;                           /* change TL (default 99) */
    MdxSynth_SetOperator(0, &op);

    /* op0 offset is 0x00, so its TL write is exactly 0x40. */
    checkf(cap_count(MDX_REG_TL) >= 1,
           "live-edit wrote >=1 TL (0x40) (got %d)", cap_count(MDX_REG_TL));

    /* Live edit must NOT touch 0x28 (no key-off/on -> no retrigger). */
    checkf(cap_count(MDX_REG_KEYON) == 0,
           "live-edit wrote ZERO 0x28 (no retrigger) (got %d)",
           cap_count(MDX_REG_KEYON));
}


/* ---- Scenario 5: Deferred retrigger on topology change ---- */
static void scenario_deferred_retrigger(void)
{
    printf("\n=== Scenario 5: Deferred retrigger on topology change ===\n");
    MdxSynth_NoteOff();
    cap_clear();
    MdxSynth_NoteOn(69);              /* key-on; g_keyState = ON */
    MdxSynth_SetAlgorithm(3);         /* sets topology-dirty */
    MdxSynth_NoteOn(69);              /* deferred: KeyOff this frame */

    /* This frame: deferred KeyOff -> 0x28 = 0x00 (header note 1). */
    checkf(cap_last_val(MDX_REG_KEYON) == 0x00,
           "2nd NoteOn deferred KeyOff last 0x28 == 0x00 (got 0x%02X)",
           (unsigned)cap_last_val(MDX_REG_KEYON));
    /* Mechanism check: that write is a key-OFF (operator bits clear). */
    checkf((cap_last_val(MDX_REG_KEYON) & 0xF0) == 0x00,
           "2nd NoteOn last 0x28 is a key-OFF (op bits clear) (got 0x%02X)",
           (unsigned)cap_last_val(MDX_REG_KEYON));

    MdxSynth_Frame(TRUE);             /* next frame: complete KeyOn */

    /* Next frame: KeyOn -> 0x28 = 0xF0 (header note 1). */
    checkf(cap_last_val(MDX_REG_KEYON) == 0xF0,
           "Frame(TRUE) KeyOn completes last 0x28 == 0xF0 (got 0x%02X)",
           (unsigned)cap_last_val(MDX_REG_KEYON));
    /* Mechanism check: that write is a key-ON with all four operators. */
    checkf((cap_last_val(MDX_REG_KEYON) & 0xF0) == 0xF0,
           "Frame(TRUE) last 0x28 is a key-ON all-operators (got 0x%02X)",
           (unsigned)cap_last_val(MDX_REG_KEYON));
}


/* ---- Scenario 6: Pitch C4 (note 60) ---- */
static void scenario_pitch_c4(void)
{
    printf("\n=== Scenario 6: NoteOn(60) (C4) ===\n");
    MdxSynth_NoteOff();
    cap_clear();
    MdxSynth_NoteOn(60);

    /* 0xA4 = (block4 << 3) | (552 >> 8 = 2) = 0x22. */
    checkf(cap_last_val(MDX_REG_A4) == 0x22,
           "last 0xA4 == 0x22 (block4<<3 | fnumHi 2) (got 0x%02X)",
           (unsigned)cap_last_val(MDX_REG_A4));

    /* 0xA0 = 552 & 0xFF = 0x28. */
    checkf(cap_last_val(MDX_REG_A0) == 0x28,
           "last 0xA0 == 0x28 (552 & 0xFF) (got 0x%02X)",
           (unsigned)cap_last_val(MDX_REG_A0));
}


/* ---- Scenario 7: Monophonic guard ---- */
static void scenario_mono_guard(void)
{
    printf("\n=== Scenario 7: Monophonic guard (channel != 1 rejected) ===\n");
    cap_clear();

    /* channel 2 -> rejected by the monophonic guard (no capture). */
    MdxYm2612_WriteReg(2, MDX_REG_TL, MDX_OP_OFFSET[0], 0x00);
    checkf(g_capN == 0,
           "channel-2 write rejected (g_capN == 0) (got %d)", g_capN);

    /* channel 1 -> accepted: reg = group(0x40)+offset(0x00) = 0x40, val 0x10. */
    MdxYm2612_WriteReg(1, MDX_REG_TL, MDX_OP_OFFSET[0], 0x10);
    checkf(g_capN == 1 && g_cap[0].reg == MDX_REG_TL && g_cap[0].val == 0x10,
           "channel-1 write captured: reg 0x40 val 0x10 (g_capN=%d reg=0x%02X val=0x%02X)",
           g_capN, g_cap[0].reg, g_cap[0].val);
}


int main(void)
{
    printf("MDX-4 synth ENGINE register-trace harness\n");
    printf("engine : src/core/mdx_synth.c (REAL, unmodified)\n");
    printf("driver : tools/host_test/ym2612_host.c (write-capture shim)\n");
    printf("adjustments: 0x28 ch1 keyon=0xF0/keyoff=0x00; A4-before-A0; TL/SSG-EG counted per group\n");

    scenario_init();
    scenario_noteon_69();
    scenario_noteoff();
    scenario_live_edit();
    scenario_deferred_retrigger();
    scenario_pitch_c4();
    scenario_mono_guard();

    printf("\n==================================================\n");
    printf("checks passed: %d   checks failed: %d\n", g_ok, g_fail);
    printf("%s\n", g_fail == 0 ? "RESULT: ALL SCENARIOS PASS" : "RESULT: FAILURES PRESENT");
    printf("==================================================\n");

    return g_fail;
}
