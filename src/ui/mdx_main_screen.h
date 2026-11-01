#ifndef _MDX_MAIN_SCREEN_H_
#define _MDX_MAIN_SCREEN_H_

/*
 * mdx_main_screen.h - main / overview screen (SPEC 4.2).
 *
 * The hub screen: shows the patch name, algorithm, selected operator, the
 * selected operator's parameters + envelope preview, feedback / LFO, and the
 * keyboard/performance status. Up/Down selects a row; A opens the matching
 * sub-screen; Left/Right changes the value in place (audible immediately via the
 * engine). The engine keeps sounding the whole time.
 */

#include <genesis.h>

void MdxMainScreen_Init(void);
void MdxMainScreen_Enter(void);
void MdxMainScreen_Handle(const void* input);

#endif /* _MDX_MAIN_SCREEN_H_ */
