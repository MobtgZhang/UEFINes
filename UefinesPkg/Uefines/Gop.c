/** @file
  GOP framebuffer helpers (adapted from NesUEFI driver/graphics.c, Intel BSD header).
**/
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/GraphicsOutput.h>
#include "Graphics.h"

STATIC UINT32  *mShadowFb   = NULL;
STATIC UINTN   mShadowBytes = 0;

EFI_STATUS
EFIAPI
graphics_init (
  OUT EFI_GRAPHICS_OUTPUT_PROTOCOL  **Gop
  )
{
  EFI_STATUS  Status;

  Status = gBS->LocateProtocol (
                  &gEfiGraphicsOutputProtocolGuid,
                  NULL,
                  (VOID **)Gop
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (*Gop == NULL) {
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
graphics_set_mode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  UINTN                                 ModeIndex;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
  UINTN                                 SizeOfInfo;
  EFI_STATUS                            Status;

  if (Gop->Mode == NULL) {
    return EFI_UNSUPPORTED;
  }

  for (ModeIndex = 0; ModeIndex < Gop->Mode->MaxMode; ModeIndex++) {
    Status = Gop->QueryMode (Gop, ModeIndex, &SizeOfInfo, &Info);
    if (EFI_ERROR (Status) && (Status == EFI_NOT_STARTED)) {
      Status = Gop->SetMode (Gop, Gop->Mode->Mode);
      Status = Gop->QueryMode (Gop, ModeIndex, &SizeOfInfo, &Info);
    }

    if (EFI_ERROR (Status)) {
      continue;
    }

    if (CompareMem (Info, Gop->Mode->Info, sizeof (*Info)) == 0) {
      return EFI_SUCCESS;
    }
  }

  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
graphics_set_pixel (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN UINT32                       X,
  IN UINT32                       Y,
  IN UINT32                       Colour
  )
{
  UINT32  *Fb;
  UINT32  Pitch;

  if (Gop == NULL || Gop->Mode == NULL || Gop->Mode->Info == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Pitch = Gop->Mode->Info->PixelsPerScanLine;
  if (X >= Gop->Mode->Info->HorizontalResolution ||
      Y >= Gop->Mode->Info->VerticalResolution) {
    return EFI_SUCCESS;
  }

  if (mShadowFb != NULL) {
    Fb = mShadowFb;
  } else {
    Fb = (UINT32 *)(UINTN)Gop->Mode->FrameBufferBase;
  }

  Fb[X + Y * Pitch] = Colour;
  return EFI_SUCCESS;
}

VOID
EFIAPI
graphics_clear_framebuffer (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  VOID  *Dst;

  if (Gop == NULL || Gop->Mode == NULL) {
    return;
  }

  Dst = (mShadowFb != NULL) ? (VOID *)mShadowFb
         : (VOID *)(UINTN)Gop->Mode->FrameBufferBase;
  SetMem (Dst, (UINTN)Gop->Mode->FrameBufferSize, 0);
}

EFI_STATUS
EFIAPI
graphics_shadow_buffer_init (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  UINTN  Sz;

  if (Gop == NULL || Gop->Mode == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Sz = (UINTN)Gop->Mode->FrameBufferSize;
  if (Sz == 0) {
    return EFI_UNSUPPORTED;
  }

  mShadowFb = (UINT32 *)AllocatePool (Sz);
  if (mShadowFb == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  mShadowBytes = Sz;
  SetMem (mShadowFb, Sz, 0);
  CopyMem ((VOID *)(UINTN)Gop->Mode->FrameBufferBase, mShadowFb, Sz);
  return EFI_SUCCESS;
}

VOID
EFIAPI
graphics_present (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  if ((Gop == NULL) || (Gop->Mode == NULL) || (mShadowFb == NULL) ||
      (mShadowBytes != (UINTN)Gop->Mode->FrameBufferSize))
  {
    return;
  }

  CopyMem (
    (VOID *)(UINTN)Gop->Mode->FrameBufferBase,
    mShadowFb,
    mShadowBytes
    );
}

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
  )
{
  UINT32  *Fb;
  UINTN   Pitch;
  UINT32  ResX;
  UINT32  ResY;
  UINT32  x;
  UINT32  y;
  UINT32  dx;
  UINT32  dy;
  UINT32  c;
  INT32   px;
  INT32   py;

  if ((Gop == NULL) || (Gop->Mode == NULL) || (Gop->Mode->Info == NULL) ||
      (NesPixels == NULL) || (Scale == 0))
  {
    return;
  }

  Pitch = Gop->Mode->Info->PixelsPerScanLine;
  ResX  = Gop->Mode->Info->HorizontalResolution;
  ResY  = Gop->Mode->Info->VerticalResolution;

  if (mShadowFb != NULL) {
    Fb = mShadowFb;
  } else {
    Fb = (UINT32 *)(UINTN)Gop->Mode->FrameBufferBase;
  }

  for (y = 0; y < NesH; y++) {
    for (x = 0; x < NesW; x++) {
      c = NesPixels[x + y * NesW];
      for (dy = 0; dy < Scale; dy++) {
        py = YOff + (INT32)(y * Scale + dy);
        if ((py < 0) || ((UINT32)py >= ResY)) {
          continue;
        }

        for (dx = 0; dx < Scale; dx++) {
          px = XOff + (INT32)(x * Scale + dx);
          if ((px < 0) || ((UINT32)px >= ResX)) {
            continue;
          }

          Fb[(UINTN)px + (UINTN)py * Pitch] = c;
        }
      }
    }
  }
}
