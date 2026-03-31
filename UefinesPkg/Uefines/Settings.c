#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/PrintLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include "Kernel.h"
#include "Settings.h"

extern UINT32   g_nes_fps_target;
extern INT32    g_nes_screen_zoom;
extern BOOLEAN  g_settings_mute;
extern UINT32   g_settings_region;
extern UINT8    g_settings_hold_frames;
extern UINT32   g_settings_keymap;

/*
  Boot-volume config: Uefines/settings.ini (KEY=VALUE lines).
  Loaded at startup, reloaded when opening Controller, saved after each change there.
*/
#define SETTINGS_DIR   L"Uefines"
#define SETTINGS_FILE  L"Uefines\\settings.ini"

STATIC
EFI_STATUS
OpenBootVolumeRoot (
  OUT EFI_FILE_PROTOCOL  **Root
  )
{
  EFI_STATUS                       Status;
  EFI_LOADED_IMAGE_PROTOCOL        *Loaded;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Sfs;

  Status = gBS->HandleProtocol (
                  gKernel.ImageHandle,
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

  return Sfs->OpenVolume (Sfs, Root);
}

STATIC
EFI_STATUS
EnsureSettingsDir (
  IN EFI_FILE_PROTOCOL  *Root
  )
{
  EFI_STATUS          Status;
  EFI_FILE_PROTOCOL   *Dir;

  Status = Root->Open (
                  Root,
                  &Dir,
                  SETTINGS_DIR,
                  EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                  EFI_FILE_DIRECTORY
                  );
  if (!EFI_ERROR (Status)) {
    Dir->Close (Dir);
    return EFI_SUCCESS;
  }

  Status = Root->Open (
                  Root,
                  &Dir,
                  SETTINGS_DIR,
                  EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                  EFI_FILE_DIRECTORY
                  );
  if (!EFI_ERROR (Status)) {
    Dir->Close (Dir);
  }

  return Status;
}

STATIC
VOID
ParseLine (
  IN CHAR8  *Line
  )
{
  while ((*Line == ' ') || (*Line == '\t')) {
    Line++;
  }

  if ((*Line == '#') || (*Line == ';') || (*Line == '\0')) {
    return;
  }

  if (AsciiStrnCmp (Line, "ZOOM=", 5) == 0) {
    g_nes_screen_zoom = (INT32)AsciiStrDecimalToUint64 (Line + 5);
    if (g_nes_screen_zoom < 0) {
      g_nes_screen_zoom = 0;
    }

    if (g_nes_screen_zoom > 16) {
      g_nes_screen_zoom = 16;
    }
  } else if (AsciiStrnCmp (Line, "REGION=", 7) == 0) {
    g_settings_region = (UINT32)AsciiStrDecimalToUint64 (Line + 7);
    g_nes_fps_target  = (g_settings_region != 0) ? 60 : 50;
  } else if (AsciiStrnCmp (Line, "MUTE=", 5) == 0) {
    g_settings_mute = (AsciiStrDecimalToUint64 (Line + 5) != 0);
  } else if (AsciiStrnCmp (Line, "HOLD=", 5) == 0) {
    UINT8  v;

    v = (UINT8)AsciiStrDecimalToUint64 (Line + 5);
    if ((v != 5) && (v != 10) && (v != 15) && (v != 20)) {
      v = 10;
    }

    g_settings_hold_frames = v;
  } else if (AsciiStrnCmp (Line, "KEYMAP=", 7) == 0) {
    g_settings_keymap = (UINT32)AsciiStrDecimalToUint64 (Line + 7);
    if (g_settings_keymap > 1) {
      g_settings_keymap = 0;
    }
  }
}

EFI_STATUS
EFIAPI
SettingsLoad (
  VOID
  )
{
  EFI_FILE_PROTOCOL  *Root;
  EFI_FILE_PROTOCOL  *File;
  EFI_STATUS         Status;
  UINTN              ReadSize;
  UINT64             Fsize;
  CHAR8              *Buf;
  UINTN              I;
  UINTN              Start;

  Status = OpenBootVolumeRoot (&Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  (VOID)EnsureSettingsDir (Root);

  Status = Root->Open (
                  Root,
                  &File,
                  SETTINGS_FILE,
                  EFI_FILE_MODE_READ,
                  0
                  );
  Root->Close (Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Fsize = 0;
  File->SetPosition (File, 0xFFFFFFFFFFFFFFFFULL);
  File->GetPosition (File, &Fsize);
  File->SetPosition (File, 0);
  ReadSize = (UINTN)Fsize;
  if (ReadSize == 0) {
    File->Close (File);
    return EFI_SUCCESS;
  }

  if (ReadSize > 4096) {
    ReadSize = 4096;
  }

  Buf = AllocatePool (ReadSize + 1);
  if (Buf == NULL) {
    File->Close (File);
    return EFI_OUT_OF_RESOURCES;
  }

  Status = File->Read (File, &ReadSize, Buf);
  File->Close (File);
  if (EFI_ERROR (Status)) {
    FreePool (Buf);
    return Status;
  }

  Buf[ReadSize] = '\0';
  Start = 0;
  for (I = 0; I <= ReadSize; I++) {
    if ((Buf[I] == '\r') || (Buf[I] == '\n') || (Buf[I] == '\0')) {
      Buf[I] = '\0';
      if (I > Start) {
        ParseLine (&Buf[Start]);
      }

      Start = I + 1;
    }
  }

  FreePool (Buf);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SettingsSave (
  VOID
  )
{
  EFI_FILE_PROTOCOL  *Root;
  EFI_FILE_PROTOCOL  *File;
  EFI_STATUS         Status;
  CHAR8              Line[128];
  UINTN              Len;

  Status = OpenBootVolumeRoot (&Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  (VOID)EnsureSettingsDir (Root);

  Status = Root->Open (
                  Root,
                  &File,
                  SETTINGS_FILE,
                  EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                  0
                  );
  Root->Close (Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  {
    CHAR8 CONST  *Hdr;
    UINTN        HdrLen;

    Hdr    = "# UEFINes config (edit or use Controller menu)\r\n";
    HdrLen = AsciiStrLen (Hdr);
    Status = File->Write (File, &HdrLen, (VOID *)Hdr);
    if (EFI_ERROR (Status)) {
      File->Close (File);
      return Status;
    }
  }

  Len = AsciiSPrint (
          Line,
          sizeof (Line),
          "ZOOM=%d\r\nREGION=%u\r\nMUTE=%u\r\nHOLD=%u\r\nKEYMAP=%u\r\n",
          (INT32)g_nes_screen_zoom,
          (UINT32)g_settings_region,
          g_settings_mute ? 1U : 0U,
          (UINT32)g_settings_hold_frames,
          (UINT32)g_settings_keymap
          );
  Status = File->Write (File, &Len, Line);
  File->Close (File);
  return Status;
}
