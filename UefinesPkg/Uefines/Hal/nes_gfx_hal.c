#include "nes_gfx_hal.h"
#include "../Graphics.h"
#include <Library/BaseMemoryLib.h>

UINT32  _nes_screen_buffer_current[NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT + 1];

VOID
nes_set_pixel (
  INT32   X,
  INT32   Y,
  UINT32  NesColour
  )
{
  if (X > -1) {
    _nes_screen_buffer_current[X + Y * NES_SCREEN_WIDTH] = NesColour;
  }
}

VOID
nes_set_bg (
  UINT32  Colour
  )
{
  INT32  I;

  for (I = 0; I < NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT; ++I) {
    nes_set_pixel (I % NES_SCREEN_WIDTH, I / NES_SCREEN_WIDTH, Colour);
  }
}

VOID
nes_gfx_swap (
  VOID
  )
{
  INT32   Z;
  INT32   Zmax;
  INT32   XOff;
  INT32   YOff;
  UINT32  ResX;
  UINT32  ResY;

  if (gKernel.graphics == NULL) {
    return;
  }

  ResX = gKernel.graphics->Mode->Info->HorizontalResolution;
  ResY = gKernel.graphics->Mode->Info->VerticalResolution;

  Zmax = (INT32)(ResX / NES_SCREEN_WIDTH);
  if ((INT32)(ResY / NES_SCREEN_HEIGHT) < Zmax) {
    Zmax = (INT32)(ResY / NES_SCREEN_HEIGHT);
  }

  if (Zmax < 1) {
    Zmax = 1;
  }

  if (g_nes_screen_zoom <= 0) {
    Z = Zmax;
  } else {
    Z = g_nes_screen_zoom;
    if (Z > Zmax) {
      Z = Zmax;
    }

    if (Z < 1) {
      Z = 1;
    }
  }

  XOff = ((INT32)ResX - (INT32)(NES_SCREEN_WIDTH * Z)) / 2;
  YOff = ((INT32)ResY - (INT32)(NES_SCREEN_HEIGHT * Z)) / 2;

  graphics_clear_framebuffer (gKernel.graphics);
  graphics_blit_nes_scaled (
    gKernel.graphics,
    _nes_screen_buffer_current,
    NES_SCREEN_WIDTH,
    NES_SCREEN_HEIGHT,
    (UINT32)Z,
    XOff,
    YOff
    );
}
