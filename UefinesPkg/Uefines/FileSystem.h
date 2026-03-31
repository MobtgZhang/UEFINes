#ifndef FILESYSTEM_H_
#define FILESYSTEM_H_

#include <Uefi.h>
#include <Protocol/SimpleFileSystem.h>

#define FS_MAX_VOLUMES   16
/* 目录项上限；超过会触发 EFI_BUFFER_TOO_SMALL 且浏览列表被置空（常见 ROM 集 >95 个即中招） */
#define FS_MAX_ENTRIES   512
#define FS_NAME_LEN      200

typedef struct {
  CHAR16     Name[FS_NAME_LEN];
  BOOLEAN    IsDir;
} FS_ENTRY;

EFI_STATUS
EFIAPI
FsRefreshVolumeHandles (
  VOID
  );

UINTN
EFIAPI
FsVolumeCount (
  VOID
  );

EFI_HANDLE
EFIAPI
FsVolumeHandle (
  IN UINTN  Index
  );

EFI_STATUS
EFIAPI
FsOpenVolumeRoot (
  IN  EFI_HANDLE          VolumeHandle,
  OUT EFI_FILE_PROTOCOL   **Root
  );

EFI_STATUS
EFIAPI
FsReadDirectory (
  IN     EFI_FILE_PROTOCOL  *Dir,
  IN     BOOLEAN            ShowParentDotDot,
  IN OUT FS_ENTRY           *Entries,
  IN     UINTN              MaxEntries,
  OUT    UINTN              *Count
  );

EFI_STATUS
EFIAPI
FsReadFileFromDir (
  IN  EFI_FILE_PROTOCOL  *Dir,
  IN  CHAR16             *FileName,
  OUT VOID               **Buffer,
  OUT UINTN              *FileSize
  );

BOOLEAN
EFIAPI
FsNameIsNesRom (
  IN CHAR16  *Name
  );

/* TRUE for "roms" bundle folder on ROM volume (case-insensitive). */
BOOLEAN
EFIAPI
FsNameIsNesDirectory (
  IN CHAR16  *Name
  );

BOOLEAN
EFIAPI
FsVolumeLooksLikeRomDisk (
  IN EFI_HANDLE  VolumeHandle
  );

#endif
