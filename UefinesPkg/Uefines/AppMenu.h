#ifndef APPMENU_H_
#define APPMENU_H_

#include <Uefi.h>

VOID
EFIAPI
AppMenuInit (
  VOID
  );

BOOLEAN
EFIAPI
AppMenuDrawStaticUi (
  VOID
  );

VOID
EFIAPI
AppMenuHandleInput (
  VOID
  );

BOOLEAN
EFIAPI
AppMenuIsPlaying (
  VOID
  );

VOID
EFIAPI
AppRunOneEmulatorFrame (
  VOID
  );

#endif
