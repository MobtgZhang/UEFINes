/** @file
  UFNT 位图字体加载（与 scripts/gen_ufnt_bin.py 配套）。缺省拉丁字母用经典字体纵向双倍（类 Times 比例由 TTF 栅格化字体提供）。
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include "adafruit_gfx.h"
#include "utf_font.h"

#define UFNT_MAGIC  0x314E4655U

#pragma pack(1)
typedef struct {
  UINT32    Magic;
  UINT32    Version;
  UINT32    Count;
} UFNT_HEADER;

typedef struct {
  UINT32    Cp;
  UINT8     Bmp[32];
} UFNT_GLYPH;
#pragma pack()

STATIC UINT8   *mFontBlob   = NULL;
STATIC UINTN   mFontSize    = 0;
STATIC UINT32  mGlyphCount  = 0;
STATIC UFNT_GLYPH  *mGlyphs = NULL;

STATIC
UFNT_GLYPH *
FindGlyph (
  IN UINT32  Cp
  )
{
  INTN  Lo;
  INTN  Hi;
  INTN  Mid;

  if ((mGlyphs == NULL) || (mGlyphCount == 0)) {
    return NULL;
  }

  Lo = 0;
  Hi = (INTN)mGlyphCount - 1;
  while (Lo <= Hi) {
    Mid = (Lo + Hi) / 2;
    if (mGlyphs[Mid].Cp == Cp) {
      return &mGlyphs[Mid];
    }

    if (mGlyphs[Mid].Cp < Cp) {
      Lo = Mid + 1;
    } else {
      Hi = Mid - 1;
    }
  }

  return NULL;
}

STATIC
VOID
Blit16Scaled (
  IN UFNT_GLYPH  *G,
  IN INT16       X,
  IN INT16       Y,
  IN UINT32      Fg,
  IN UINT32      Bg,
  IN UINTN       Sx,
  IN UINTN       Sy
  )
{
  UINTN   Row;
  UINTN   Col;
  UINT16  Bits;

  if (Sx == 0) {
    Sx = 1;
  }

  if (Sy == 0) {
    Sy = 1;
  }

  startWrite();
  for (Row = 0; Row < 16; Row++) {
    Bits = (UINT16)((((UINT16)G->Bmp[Row * 2]) << 8) | (UINT16)G->Bmp[Row * 2 + 1]);
    for (Col = 0; Col < 16; Col++) {
      if ((Bits >> (15 - Col)) & 1) {
        writeFillRect (
          (INT16)(X + (INT16)(Col * Sx)),
          (INT16)(Y + (INT16)(Row * Sy)),
          (INT16)Sx,
          (INT16)Sy,
          Fg
          );
      } else if (Bg != Fg) {
        writeFillRect (
          (INT16)(X + (INT16)(Col * Sx)),
          (INT16)(Y + (INT16)(Row * Sy)),
          (INT16)Sx,
          (INT16)Sy,
          Bg
          );
      }
    }
  }

  endWrite();
}

STATIC
VOID
DrawTofuScaled (
  IN INT16   X,
  IN INT16   Y,
  IN UINT32  Fg,
  IN UINT32  Bg,
  IN UINTN   Sx,
  IN UINTN   Sy
  )
{
  INT16  W;
  INT16  H;

  if (Sx == 0) {
    Sx = 1;
  }

  if (Sy == 0) {
    Sy = 1;
  }

  W = (INT16)(16 * Sx);
  H = (INT16)(16 * Sy);
  fillRect (X, Y, W, H, Bg);
  drawRect (X, Y, W, H, Fg);
}

EFI_STATUS
EFIAPI
UtfFontInit (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS                        Status;
  EFI_LOADED_IMAGE_PROTOCOL         *Loaded;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   *Sfs;
  EFI_FILE_PROTOCOL                 *Root;
  EFI_FILE_PROTOCOL                 *Fh;
  UINTN                             ReadSize;
  UFNT_HEADER                       Hdr;

  UtfFontShutdown ();

  Status = gBS->HandleProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&Loaded
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol (
                  Loaded->DeviceHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **)&Sfs
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Sfs->OpenVolume (Sfs, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Root->Open (
                  Root,
                  &Fh,
                  L"EFI\\Boot\\Fonts\\UFNT.BIN",
                  EFI_FILE_MODE_READ,
                  0
                  );
  if (EFI_ERROR (Status)) {
    Status = Root->Open (
                    Root,
                    &Fh,
                    L"Fonts\\UFNT.BIN",
                    EFI_FILE_MODE_READ,
                    0
                    );
  }

  Root->Close (Root);
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  ReadSize = sizeof (Hdr);
  Status   = Fh->Read (Fh, &ReadSize, &Hdr);
  if (EFI_ERROR (Status) || (ReadSize != sizeof (Hdr))) {
    Fh->Close (Fh);
    return EFI_VOLUME_CORRUPTED;
  }

  if ((Hdr.Magic != UFNT_MAGIC) || (Hdr.Version != 1) || (Hdr.Count == 0)) {
    Fh->Close (Fh);
    return EFI_UNSUPPORTED;
  }

  mGlyphCount = Hdr.Count;
  mFontSize   = sizeof (UFNT_HEADER) + (UINTN)mGlyphCount * sizeof (UFNT_GLYPH);
  mFontBlob   = AllocatePool (mFontSize);
  if (mFontBlob == NULL) {
    Fh->Close (Fh);
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (mFontBlob, &Hdr, sizeof (Hdr));
  ReadSize = mFontSize - sizeof (Hdr);
  Status   = Fh->Read (Fh, &ReadSize, mFontBlob + sizeof (Hdr));
  Fh->Close (Fh);
  if (EFI_ERROR (Status) || (ReadSize != (mFontSize - sizeof (Hdr)))) {
    UtfFontShutdown ();
    return EFI_VOLUME_CORRUPTED;
  }

  mGlyphs = (UFNT_GLYPH *)(mFontBlob + sizeof (UFNT_HEADER));
  return EFI_SUCCESS;
}

VOID
EFIAPI
UtfFontShutdown (
  VOID
  )
{
  if (mFontBlob != NULL) {
    FreePool (mFontBlob);
    mFontBlob   = NULL;
    mFontSize   = 0;
    mGlyphs     = NULL;
    mGlyphCount = 0;
  }
}

UINTN
EFIAPI
UtfFontGlyphWidth (
  IN UINT32  CodePoint
  )
{
  UINTN  Sx;

  Sx = textsize_x;
  if (Sx == 0) {
    Sx = 1;
  }

  if (FindGlyph (CodePoint) != NULL) {
    return 16 * Sx;
  }

  if ((CodePoint >= 0x20) && (CodePoint < 0x7F)) {
    return 6 * Sx;
  }

  return 16 * Sx;
}

BOOLEAN
EFIAPI
UtfFontDrawGlyphAt (
  IN UINT32   CodePoint,
  IN INT16    X,
  IN INT16    Y,
  IN UINT32   Fg,
  IN UINT32   Bg
  )
{
  UFNT_GLYPH  *G;
  UINTN       Sx;
  UINTN       Sy;

  Sx = textsize_x;
  Sy = textsize_y;
  if (Sx == 0) {
    Sx = 1;
  }

  if (Sy == 0) {
    Sy = 1;
  }

  G = FindGlyph (CodePoint);
  if (G != NULL) {
    Blit16Scaled (G, X, Y, Fg, Bg, Sx, Sy);
    return TRUE;
  }

  if ((CodePoint >= 0x20) && (CodePoint < 0x7F)) {
    drawClassicGlyphAt (X, Y, (UINT8)CodePoint, Fg, Bg, (UINT8)Sx, (UINT8)(Sy * 2));
    return TRUE;
  }

  if (CodePoint == 0x20) {
    fillRect (X, Y, (INT16)(6 * Sx), (INT16)(16 * Sy), Bg);
    return TRUE;
  }

  DrawTofuScaled (X, Y, Fg, Bg, Sx, Sy);
  return TRUE;
}
