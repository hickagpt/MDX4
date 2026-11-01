/*
 * main.c - MDX-4 boot scaffold (Phase 1 skeleton).
 *
 * Boots SGDK, does a minimal VDP init (320x224 tile mode), then idles in a
 * VBlank-tied loop. The actual sound test loop is added in a later task.
 */

#include <genesis.h>


int main(bool hardReset)
{
    (void)hardReset;

    /* Minimal, safe boot: disable interrupts around VDP access, re-enable after. */
    SYS_disableInts();

    /* SGDK default is 320x224 tile mode; make the VDP state explicit. */
    VDP_init();
    VDP_clearTextLine(0);
    VDP_drawText("MDX-4", 1, 0);

    SYS_enableInts();

    while (TRUE)
    {
        SYS_doVBlankProcess();
    }

    return 0;
}
