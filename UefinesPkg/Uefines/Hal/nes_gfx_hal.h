#ifndef NES_GFX_HAL_H_
#define NES_GFX_HAL_H_

#include <Uefi.h>
#include "nes.h"
#include "../Kernel.h"

VOID
nes_set_pixel (
  INT32   X,
  INT32   Y,
  UINT32  NesColour
  );

VOID
nes_set_bg (
  UINT32  Colour
  );

VOID
nes_gfx_swap (
  VOID
  );

#endif
