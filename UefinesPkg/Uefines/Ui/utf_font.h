/** @file
  UTF-16 文本绘制：从启动卷加载 UFNT 位图字体（开源宋体等栅格化产物），缺省时回退 ASCII。
**/
#ifndef UTF_FONT_H_
#define UTF_FONT_H_

#include <Uefi.h>

EFI_STATUS
EFIAPI
UtfFontInit (
  IN EFI_HANDLE  ImageHandle
  );

VOID
EFIAPI
UtfFontShutdown (
  VOID
  );

UINTN
EFIAPI
UtfFontGlyphWidth (
  IN UINT32  CodePoint
  );

BOOLEAN
EFIAPI
UtfFontDrawGlyphAt (
  IN UINT32   CodePoint,
  IN INT16    X,
  IN INT16    Y,
  IN UINT32   Fg,
  IN UINT32   Bg
  );

#endif
