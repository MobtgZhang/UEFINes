#include <Uefi.h>
#include "Kernel.h"

KERNEL_CONTEXT  gKernel;
UINT32          g_nes_fps_target   = 60;
INT32           g_nes_screen_zoom  = 0;
BOOLEAN         g_settings_mute    = TRUE;
UINT32          g_settings_region  = 1;
UINT8           g_settings_hold_frames = 10;
UINT32          g_settings_keymap      = 0;
