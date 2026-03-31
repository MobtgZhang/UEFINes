#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <Uefi.h>

EFI_STATUS
EFIAPI
SettingsLoad (
  VOID
  );

EFI_STATUS
EFIAPI
SettingsSave (
  VOID
  );

#endif
