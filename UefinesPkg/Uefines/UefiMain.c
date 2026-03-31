/** @file
  UEFI NES player entry (UEFINes).
**/
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "AppMenu.h"
#include "Graphics.h"
#include "Input.h"
#include "Kernel.h"
#include "Settings.h"
#include "Timer.h"
#include "Ui/adafruit_gfx.h"
#include "Ui/utf_font.h"

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINT64      LastFrame;

  (VOID)SystemTable;
  gKernel.ImageHandle = ImageHandle;

  Print (L"UEFINes starting...\r\n");

  Status = UtfFontInit (ImageHandle);
  if (EFI_ERROR (Status)) {
    Print (L"UFNT font (optional): %r (Chinese: put UFNT.BIN in EFI/Boot/Fonts/)\r\n", Status);
  }
  gBS->SetWatchdogTimer (0, 0, 0, NULL);

  Status = TimerInit (TICK_PER_SECOND);
  if (EFI_ERROR (Status)) {
    Print (L"TimerInit: %r\r\n", Status);
  }

  Status = graphics_init (&gKernel.graphics);
  if (EFI_ERROR (Status)) {
    Print (L"No GOP: %r\r\n", Status);
    return Status;
  }

  Status = graphics_set_mode (gKernel.graphics);
  if (EFI_ERROR (Status)) {
    Print (L"graphics_set_mode: %r\r\n", Status);
  }

  adafruit_gfx_init (
    (INT16)gKernel.graphics->Mode->Info->HorizontalResolution,
    (INT16)gKernel.graphics->Mode->Info->VerticalResolution
    );

  Status = graphics_shadow_buffer_init (gKernel.graphics);
  if (EFI_ERROR (Status)) {
    Print (L"graphics_shadow_buffer_init: %r (direct GOP)\r\n", Status);
  }

  (VOID)SettingsLoad ();
  InputInit ();
  AppMenuInit ();

  LastFrame = 0;
  for ( ; ; ) {
    if (AppMenuIsPlaying ()) {
      InputPoll ();
      while (TimerTicks () == LastFrame) {
        gBS->Stall (120);
        InputPoll ();
      }

      LastFrame = TimerTicks ();
      AppMenuHandleInput ();
      AppRunOneEmulatorFrame ();
      graphics_present (gKernel.graphics);
      InputEndFrame ();
    } else {
      InputPoll ();
      AppMenuHandleInput ();
      if (AppMenuDrawStaticUi ()) {
        graphics_present (gKernel.graphics);
      }

      gBS->Stall (4000);
      InputEndFrame ();
    }
  }
}
