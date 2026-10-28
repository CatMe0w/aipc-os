#pragma once

/* The second framebuffer. The menu and the fade draw into the buffer that
 * is not shown, then flip to it at the start of a frame, so nothing tears
 * or flickers. A payload must not load here. */
#define FB_BACK_ADDR  0x33C00000u

/* Dims the screen to black. The screen ends on FB_ADDR. */
void fade_out(void);
