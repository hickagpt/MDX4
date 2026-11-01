/*
 * mdx_input.c - controller navigation (SPEC 4.3, Metis 1.9).
 *
 * Edge-triggered polling with auto-repeat on the four directions. Up+Down and
 * Left+Right pressed together cancel to no movement. See mdx_input.h.
 */

#include "mdx_input.h"

#include <genesis.h>
#include <joy.h>


/* Auto-repeat cadence (frames @ ~60 Hz VBlank). */
#define MDX_REPEAT_DELAY  16   /* frames before a held direction starts repeating */
#define MDX_REPEAT_RATE    4   /* frames between repeats once repeating           */

#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3

static const u16 s_dirMask[4] =
{
    BUTTON_UP, BUTTON_DOWN, BUTTON_LEFT, BUTTON_RIGHT
};

static u16 s_prevPad = 0;
static s16  s_repeat[4] =
{
    MDX_REPEAT_DELAY, MDX_REPEAT_DELAY, MDX_REPEAT_DELAY, MDX_REPEAT_DELAY
};


/* Compute one direction's edge/repeat result for this frame. */
static bool mdx_dir_edge(u16 curPad, u8 dir)
{
    const bool held = (curPad & s_dirMask[dir]) != 0;
    const bool was  = (s_prevPad & s_dirMask[dir]) != 0;

    if (held && !was)
    {
        /* new press: report immediately, arm the repeat delay */
        s_repeat[dir] = MDX_REPEAT_DELAY;
        return TRUE;
    }

    if (held && was)
    {
        /* still held: count down; emit a repeat when the timer elapses */
        s_repeat[dir]--;
        if (s_repeat[dir] <= 0)
        {
            s_repeat[dir] = MDX_REPEAT_RATE;
            return TRUE;
        }
        return FALSE;
    }

    /* released: re-arm for the next press */
    s_repeat[dir] = MDX_REPEAT_DELAY;
    return FALSE;
}


void MdxInput_Init(void)
{
    s_prevPad = 0;
    s_repeat[0] = s_repeat[1] = s_repeat[2] = s_repeat[3] = MDX_REPEAT_DELAY;
}


void MdxInput_Poll(MdxInput* out)
{
    u16 cur;
    bool up, down, left, right;

    if (out == NULL) return;

    cur = JOY_readJoypad(JOY_1);

    up    = mdx_dir_edge(cur, DIR_UP);
    down  = mdx_dir_edge(cur, DIR_DOWN);
    left  = mdx_dir_edge(cur, DIR_LEFT);
    right = mdx_dir_edge(cur, DIR_RIGHT);

    /* Metis 1.9 cancel: opposing directions together => no movement. */
    if (up && down)   { up = FALSE; down = FALSE; }
    if (left && right){ left = FALSE; right = FALSE; }

    out->up    = up;
    out->down  = down;
    out->left  = left;
    out->right = right;

    /* Buttons: pure edge (no repeat). */
    out->a     = (cur & BUTTON_A)     && !(s_prevPad & BUTTON_A);
    out->b     = (cur & BUTTON_B)     && !(s_prevPad & BUTTON_B);
    out->c     = (cur & BUTTON_C)     && !(s_prevPad & BUTTON_C);
    out->start = (cur & BUTTON_START) && !(s_prevPad & BUTTON_START);

    s_prevPad = cur;
}


bool MdxInput_Held(u16 buttonMask)
{
    return (s_prevPad & buttonMask) != 0;
}
