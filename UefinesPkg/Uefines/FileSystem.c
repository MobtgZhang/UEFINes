#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Guid/FileInfo.h>
#include <Protocol/SimpleFileSystem.h>
#include "FileSystem.h"

STATIC EFI_HANDLE  *mVolHandles = NULL;
STATIC UINTN       mVolCount    = 0;

EFI_STATUS
EFIAPI
FsRefreshVolumeHandles (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       Total;
  EFI_HANDLE  *Buf;

  if (mVolHandles != NULL) {
    FreePool (mVolHandles);
    mVolHandles = NULL;
    mVolCount   = 0;
  }

  Buf = NULL;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &Total,
                  &Buf
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Total > FS_MAX_VOLUMES) {
    EFI_HANDLE  *Tmp;

    Tmp = AllocatePool (FS_MAX_VOLUMES * sizeof (EFI_HANDLE));
    if (Tmp == NULL) {
      FreePool (Buf);
      mVolCount = 0;
      return EFI_OUT_OF_RESOURCES;
    }

    CopyMem (Tmp, Buf, FS_MAX_VOLUMES * sizeof (EFI_HANDLE));
    FreePool (Buf);
    mVolHandles = Tmp;
    mVolCount   = FS_MAX_VOLUMES;
    return EFI_SUCCESS;
  }

  mVolHandles = Buf;
  mVolCount   = Total;
  return EFI_SUCCESS;
}

UINTN
EFIAPI
FsVolumeCount (
  VOID
  )
{
  return mVolCount;
}

EFI_HANDLE
EFIAPI
FsVolumeHandle (
  IN UINTN  Index
  )
{
  if ((mVolHandles == NULL) || (Index >= mVolCount)) {
    return NULL;
  }

  return mVolHandles[Index];
}

EFI_STATUS
EFIAPI
FsOpenVolumeRoot (
  IN  EFI_HANDLE          VolumeHandle,
  OUT EFI_FILE_PROTOCOL   **Root
  )
{
  EFI_STATUS                       Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Sfs;

  Status = gBS->HandleProtocol (
                  VolumeHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **)&Sfs
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Sfs->OpenVolume (Sfs, Root);
}

STATIC
INTN
EFIAPI
FsCompareEntries (
  IN CONST VOID  *Buffer1,
  IN CONST VOID  *Buffer2
  )
{
  CONST FS_ENTRY  *A;
  CONST FS_ENTRY  *B;

  A = Buffer1;
  B = Buffer2;
  if (A->IsDir != B->IsDir) {
    return B->IsDir ? 1 : -1;
  }

  return StrCmp (A->Name, B->Name);
}

EFI_STATUS
EFIAPI
FsReadDirectory (
  IN     EFI_FILE_PROTOCOL  *Dir,
  IN     BOOLEAN            ShowParentDotDot,
  IN OUT FS_ENTRY           *Entries,
  IN     UINTN              MaxEntries,
  OUT    UINTN              *Count
  )
{
  EFI_STATUS     Status;
  UINTN          BufSize;
  EFI_FILE_INFO  *Info;
  UINTN          Slot;

  if ((Dir == NULL) || (Entries == NULL) || (Count == NULL) || (MaxEntries < 4)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = Dir->SetPosition (Dir, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Slot = 0;
  if (ShowParentDotDot) {
    StrCpyS (Entries[Slot].Name, FS_NAME_LEN, L"..");
    Entries[Slot].IsDir = TRUE;
    Slot++;
  }

  BufSize = sizeof (EFI_FILE_INFO) + FS_NAME_LEN * sizeof (CHAR16);
  Info    = AllocatePool (BufSize);
  if (Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  for ( ; ; ) {
    BufSize = sizeof (EFI_FILE_INFO) + FS_NAME_LEN * sizeof (CHAR16);
    Status  = Dir->Read (Dir, &BufSize, Info);
    if (EFI_ERROR (Status)) {
      FreePool (Info);
      return Status;
    }

    if (BufSize == 0) {
      break;
    }

    if ((StrCmp (Info->FileName, L".") == 0) ||
        (StrCmp (Info->FileName, L"..") == 0)) {
      continue;
    }

    if (Slot >= MaxEntries) {
      FreePool (Info);
      return EFI_BUFFER_TOO_SMALL;
    }

    StrCpyS (Entries[Slot].Name, FS_NAME_LEN, Info->FileName);
    Entries[Slot].IsDir = (BOOLEAN)((Info->Attribute & EFI_FILE_DIRECTORY) != 0);
    Slot++;
  }

  FreePool (Info);
  if ((ShowParentDotDot) && (Slot > 1)) {
    FS_ENTRY  Scratch;

    QuickSort (
      Entries + 1,
      Slot - 1,
      sizeof (FS_ENTRY),
      FsCompareEntries,
      &Scratch
      );
  } else if ((!ShowParentDotDot) && (Slot > 0)) {
    FS_ENTRY  Scratch;

    QuickSort (
      Entries,
      Slot,
      sizeof (FS_ENTRY),
      FsCompareEntries,
      &Scratch
      );
  }

  *Count = Slot;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
FsReadFileFromDir (
  IN  EFI_FILE_PROTOCOL  *Dir,
  IN  CHAR16             *FileName,
  OUT VOID               **Buffer,
  OUT UINTN              *FileSize
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *File;
  UINTN              InfoSize;
  EFI_FILE_INFO      *Finfo;
  UINT64             Fsz;
  UINTN              Read;
  VOID               *Mem;

  Status = Dir->Open (
                  Dir,
                  &File,
                  FileName,
                  EFI_FILE_MODE_READ,
                  0
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  InfoSize = sizeof (EFI_FILE_INFO) + FS_NAME_LEN * sizeof (CHAR16);
  Finfo    = AllocatePool (InfoSize);
  if (Finfo == NULL) {
    File->Close (File);
    return EFI_OUT_OF_RESOURCES;
  }

  Status = File->GetInfo (
                  File,
                  &gEfiFileInfoGuid,
                  &InfoSize,
                  Finfo
                  );
  if (EFI_ERROR (Status)) {
    FreePool (Finfo);
    File->Close (File);
    return Status;
  }

  Fsz = Finfo->FileSize;
  FreePool (Finfo);
  if (Fsz > (16ULL * 1024ULL * 1024ULL)) {
    File->Close (File);
    return EFI_UNSUPPORTED;
  }

  Read = (UINTN)Fsz;
  Mem  = AllocatePool (Read);
  if (Mem == NULL) {
    File->Close (File);
    return EFI_OUT_OF_RESOURCES;
  }

  Status = File->Read (File, &Read, Mem);
  File->Close (File);
  if (EFI_ERROR (Status)) {
    FreePool (Mem);
    return Status;
  }

  *Buffer   = Mem;
  *FileSize = Read;
  return EFI_SUCCESS;
}

BOOLEAN
EFIAPI
FsNameIsNesRom (
  IN CHAR16  *Name
  )
{
  UINTN  Len;
  CHAR16 *S;

  Len = StrLen (Name);
  if (Len < 4) {
    return FALSE;
  }

  S = Name + Len - 4;
  return (BOOLEAN)(
           (S[0] == L'.') &&
           ((S[1] == L'n') || (S[1] == L'N')) &&
           ((S[2] == L'e') || (S[2] == L'E')) &&
           ((S[3] == L's') || (S[3] == L'S'))
           );
}

BOOLEAN
EFIAPI
FsNameIsNesDirectory (
  IN CHAR16  *Name
  )
{
  UINTN  L;

  if (Name == NULL) {
    return FALSE;
  }

  L = StrLen (Name);
  if (L != 4) {
    return FALSE;
  }

  return (BOOLEAN)(
           (Name[0] == L'r' || Name[0] == L'R') &&
           (Name[1] == L'o' || Name[1] == L'O') &&
           (Name[2] == L'm' || Name[2] == L'M') &&
           (Name[3] == L's' || Name[3] == L'S') &&
           (Name[4] == L'\0')
           );
}

BOOLEAN
EFIAPI
FsVolumeLooksLikeRomDisk (
  IN EFI_HANDLE  VolumeHandle
  )
{
  EFI_STATUS          Status;
  EFI_FILE_PROTOCOL   *Root;
  UINTN               BufSize;
  EFI_FILE_INFO       *Info;

  Status = FsOpenVolumeRoot (VolumeHandle, &Root);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  BufSize = sizeof (EFI_FILE_INFO) + FS_NAME_LEN * sizeof (CHAR16);
  Info    = AllocatePool (BufSize);
  if (Info == NULL) {
    Root->Close (Root);
    return FALSE;
  }

  Status = Root->SetPosition (Root, 0);
  if (EFI_ERROR (Status)) {
    FreePool (Info);
    Root->Close (Root);
    return FALSE;
  }

  for ( ; ; ) {
    BufSize = sizeof (EFI_FILE_INFO) + FS_NAME_LEN * sizeof (CHAR16);
    Status  = Root->Read (Root, &BufSize, Info);
    if (EFI_ERROR (Status)) {
      FreePool (Info);
      Root->Close (Root);
      return FALSE;
    }

    if (BufSize == 0) {
      break;
    }

    if ((StrCmp (Info->FileName, L".") == 0) ||
        (StrCmp (Info->FileName, L"..") == 0)) {
      continue;
    }

    if (FsNameIsNesRom (Info->FileName)) {
      FreePool (Info);
      Root->Close (Root);
      return TRUE;
    }

    if (((Info->Attribute & EFI_FILE_DIRECTORY) != 0) &&
        FsNameIsNesDirectory (Info->FileName)) {
      FreePool (Info);
      Root->Close (Root);
      return TRUE;
    }
  }

  FreePool (Info);
  Root->Close (Root);
  return FALSE;
}
