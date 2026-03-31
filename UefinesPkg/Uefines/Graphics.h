#ifndef GRAPHICS_H_
#define GRAPHICS_H_

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

EFI_STATUS
EFIAPI
graphics_set_pixel (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN UINT32                       X,
  IN UINT32                       Y,
  IN UINT32                       Colour
  );

VOID
EFIAPI
graphics_clear_framebuffer (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  );

EFI_STATUS
EFIAPI
graphics_init (
  OUT EFI_GRAPHICS_OUTPUT_PROTOCOL  **Gop
  );

EFI_STATUS
EFIAPI
graphics_set_mode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  );

EFI_STATUS
EFIAPI
graphics_shadow_buffer_init (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  );

VOID
EFIAPI
graphics_present (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  );

VOID
EFIAPI
graphics_blit_nes_scaled (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN CONST UINT32                  *NesPixels,
  IN UINT32                        NesW,
  IN UINT32                        NesH,
  IN UINT32                        Scale,
  IN INT32                         XOff,
  IN INT32                         YOff
  );

#endif
