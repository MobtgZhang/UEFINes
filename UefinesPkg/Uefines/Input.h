#ifndef INPUT_H_
#define INPUT_H_

#include <Uefi.h>

VOID
EFIAPI
InputInit (
  VOID
  );

VOID
EFIAPI
InputPoll (
  VOID
  );

VOID
EFIAPI
InputEndFrame (
  VOID
  );

BOOLEAN
EFIAPI
InputMenuUp (
  VOID
  );

BOOLEAN
EFIAPI
InputMenuDown (
  VOID
  );

BOOLEAN
EFIAPI
InputMenuEnter (
  VOID
  );

BOOLEAN
EFIAPI
InputMenuEsc (
  VOID
  );

BOOLEAN
EFIAPI
InputMenuPgUp (
  VOID
  );

BOOLEAN
EFIAPI
InputMenuPgDown (
  VOID
  );

#endif
