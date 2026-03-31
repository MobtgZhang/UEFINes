#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "AppMenu.h"
#include "Hal/nes_gfx_hal.h"
#include "Emulator/fce.h"
#include "FileSystem.h"
#include "Graphics.h"
#include "Input.h"
#include "Kernel.h"
#include "Settings.h"
#include "Timer.h"
#include "Ui/adafruit_gfx.h"

/* Menu UFNT 16x16 glyphs; 1x text scale (classic size); row spacing tracks text size */
#define UI_MENU_TSX   1
#define UI_MENU_TSY   1
#define UI_MENU_ROW   ((INT16)(16 * UI_MENU_TSY + 14))

extern UINT8    g_settings_hold_frames;
extern UINT32   g_settings_keymap;

typedef enum {
  ViewMain,
  ViewVolPick,
  ViewBrowse,
  ViewSettings,
  ViewAbout,
  ViewErr,
  ViewPlay
} APP_VIEW;

STATIC APP_VIEW        gView         = ViewMain;
STATIC INTN            gMainSel      = 0;
STATIC INTN            gVolSel       = 0;
STATIC INTN            gBrowseSel    = 0;
STATIC INTN            gSettingsSel   = 0;
STATIC FS_ENTRY        gEntries[FS_MAX_ENTRIES];
STATIC UINTN           gEntryCount   = 0;
STATIC EFI_FILE_PROTOCOL  *gVolRoot = NULL;
STATIC EFI_FILE_PROTOCOL  *gCwd     = NULL;
STATIC UINTN           gPathDepth    = 0;
STATIC CHAR16          gPathStack[16][FS_NAME_LEN];
STATIC CHAR8           gErrLine[160];
STATIC BOOLEAN         mUiDirty = TRUE;

STATIC VOID MaybeAutoEnterRomSubdir (VOID);

STATIC
VOID
DrawBackground (
  VOID
  )
{
  UINT32  W;
  UINT32  H;

  if (gKernel.graphics == NULL) {
    return;
  }

  W = gKernel.graphics->Mode->Info->HorizontalResolution;
  H = gKernel.graphics->Mode->Info->VerticalResolution;
  fillRect (0, 0, (INT16)W, (INT16)H, 0xFF0A1A12);
}

STATIC
VOID
DrawTitle (
  IN CONST CHAR16  *Title
  )
{
  setTextColor (0xFFFFFFFF);
  setTextSize (UI_MENU_TSX, UI_MENU_TSY);
  setCursor (24, 20);
  print_16 (Title);
}

STATIC
VOID
DrawFooter (
  VOID
  )
{
  setTextColor (0xFF9EC4B0);
  setTextSize (UI_MENU_TSX, UI_MENU_TSY);
  setCursor (
    24,
    (INT16)(gKernel.graphics->Mode->Info->VerticalResolution - (16 * UI_MENU_TSY + 28))
    );
  print_16 (L"Up/Down move   Enter confirm   Esc back   PgUp/PgDn page");
}

STATIC
VOID
DrawListLine16 (
  IN INT16          Y,
  IN BOOLEAN        Selected,
  IN CONST CHAR16   *Text
  )
{
  UINT32  W;

  W = gKernel.graphics->Mode->Info->HorizontalResolution;
  if (Selected) {
    fillRect (
      16,
      (INT16)(Y - 2),
      (INT16)(W - 32),
      (INT16)(16 * UI_MENU_TSY + 12),
      0xFF1E4D2E
      );
  }

  setTextColor (0xFFE8F0E8);
  setTextSize (UI_MENU_TSX, UI_MENU_TSY);
  setCursor (32, Y);
  print_16 (Text);
}

STATIC
VOID
CloseVolumeHandles (
  VOID
  )
{
  if ((gCwd != NULL) && (gCwd != gVolRoot)) {
    gCwd->Close (gCwd);
  }

  gCwd = NULL;
  if (gVolRoot != NULL) {
    gVolRoot->Close (gVolRoot);
  }

  gVolRoot = NULL;
  gPathDepth = 0;
}

STATIC
EFI_STATUS
ReopenCwdFromStack (
  VOID
  )
{
  EFI_STATUS         Status;
  UINTN              I;
  EFI_FILE_PROTOCOL  *Walk;
  EFI_FILE_PROTOCOL  *Next;

  if (gVolRoot == NULL) {
    return EFI_NOT_READY;
  }

  if ((gCwd != NULL) && (gCwd != gVolRoot)) {
    gCwd->Close (gCwd);
  }

  gCwd = NULL;
  if (gPathDepth == 0) {
    Status = gVolRoot->Open (
                    gVolRoot,
                    &gCwd,
                    L".",
                    EFI_FILE_MODE_READ,
                    0
                    );
    return Status;
  }

  Walk = gVolRoot;
  for (I = 0; I < gPathDepth; I++) {
    Status = Walk->Open (
                    Walk,
                    &Next,
                    gPathStack[I],
                    EFI_FILE_MODE_READ,
                    0
                    );
    if (EFI_ERROR (Status)) {
      if (Walk != gVolRoot) {
        Walk->Close (Walk);
      }

      return Status;
    }

    if (Walk != gVolRoot) {
      Walk->Close (Walk);
    }

    Walk = Next;
  }

  gCwd = Walk;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
OpenPickedVolume (
  VOID
  )
{
  EFI_HANDLE  H;

  CloseVolumeHandles ();
  H = FsVolumeHandle ((UINTN)gVolSel);
  if (H == NULL) {
    return EFI_NOT_FOUND;
  }

  return FsOpenVolumeRoot (H, &gVolRoot);
}

STATIC
VOID
RefreshBrowseList (
  VOID
  )
{
  BOOLEAN  ShowDot;

  if (gCwd == NULL) {
    return;
  }

  ShowDot = (BOOLEAN)((gPathDepth > 0) || (FsVolumeCount () > 1));
  if (EFI_ERROR (FsReadDirectory (gCwd, ShowDot, gEntries, FS_MAX_ENTRIES, &gEntryCount))) {
    gEntryCount = 0;
  }

  if (gBrowseSel >= (INTN)gEntryCount) {
    gBrowseSel = 0;
  }

  mUiDirty = TRUE;
}

STATIC
VOID
EnterBrowseFromMain (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       I;

  FsRefreshVolumeHandles ();
  if (FsVolumeCount () == 0) {
    AsciiStrCpyS (gErrLine, sizeof (gErrLine), "No FAT volumes found.");
    gView = ViewErr;
    return;
  }

  if (FsVolumeCount () == 1) {
    gVolSel = 0;
    Status  = OpenPickedVolume ();
    if (EFI_ERROR (Status)) {
      AsciiStrCpyS (gErrLine, sizeof (gErrLine), "Open volume failed.");
      gView = ViewErr;
      return;
    }

    gPathDepth = 0;
    Status     = ReopenCwdFromStack ();
    if (EFI_ERROR (Status)) {
      AsciiStrCpyS (gErrLine, sizeof (gErrLine), "Open root failed.");
      gView = ViewErr;
      return;
    }

    gView       = ViewBrowse;
    gBrowseSel  = 0;
    RefreshBrowseList ();
    MaybeAutoEnterRomSubdir ();
    return;
  }

  for (I = 0; I < FsVolumeCount (); I++) {
    if (!FsVolumeLooksLikeRomDisk (FsVolumeHandle (I))) {
      continue;
    }

    gVolSel = (INTN)I;
    Status  = OpenPickedVolume ();
    if (EFI_ERROR (Status)) {
      continue;
    }

    gPathDepth = 0;
    Status     = ReopenCwdFromStack ();
    if (EFI_ERROR (Status)) {
      CloseVolumeHandles ();
      continue;
    }

    gView      = ViewBrowse;
    gBrowseSel = 0;
    RefreshBrowseList ();
    MaybeAutoEnterRomSubdir ();
    return;
  }

  gView   = ViewVolPick;
  gVolSel = 0;
}

STATIC
VOID
EnterSubdir (
  IN CHAR16  *Name
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Next;

  if (gCwd == NULL) {
    return;
  }

  Status = gCwd->Open (
                  gCwd,
                  &Next,
                  Name,
                  EFI_FILE_MODE_READ,
                  0
                  );
  if (EFI_ERROR (Status)) {
    return;
  }

  if ((gCwd != NULL) && (gCwd != gVolRoot)) {
    gCwd->Close (gCwd);
  }

  gCwd = Next;
  StrCpyS (gPathStack[gPathDepth], FS_NAME_LEN, Name);
  gPathDepth++;
  gBrowseSel = 0;
  RefreshBrowseList ();
}

STATIC
VOID
MaybeAutoEnterRomSubdir (
  VOID
  )
{
  if (gEntryCount != 1) {
    return;
  }

  if (!gEntries[0].IsDir) {
    return;
  }

  if (!FsNameIsNesDirectory (gEntries[0].Name)) {
    return;
  }

  EnterSubdir (gEntries[0].Name);
}

STATIC
VOID
GoParentDir (
  VOID
  )
{
  if (gPathDepth > 0) {
    gPathDepth--;
    if (EFI_ERROR (ReopenCwdFromStack ())) {
      gEntryCount = 0;
    } else {
      RefreshBrowseList ();
    }

    return;
  }

  if (FsVolumeCount () > 1) {
    CloseVolumeHandles ();
    gView = ViewVolPick;
  } else {
    CloseVolumeHandles ();
    gView = ViewMain;
  }
}

STATIC
VOID
TryLoadRom (
  IN CHAR16  *Name
  )
{
  VOID        *Buf;
  UINTN       Sz;
  EFI_STATUS  Status;

  if (gCwd == NULL) {
    return;
  }

  Status = FsReadFileFromDir (gCwd, Name, &Buf, &Sz);
  if (EFI_ERROR (Status)) {
    AsciiStrCpyS (gErrLine, sizeof (gErrLine), "Read file failed.");
    gView = ViewErr;
    return;
  }

  if (fce_load_rom ((CHAR8 *)Buf) != 0) {
    FreePool (Buf);
    AsciiStrCpyS (gErrLine, sizeof (gErrLine), "ROM load error (mapper?).");
    gView = ViewErr;
    return;
  }

  FreePool (Buf);
  fce_init ();
  graphics_clear_framebuffer (gKernel.graphics);
  gView = ViewPlay;
}

STATIC
VOID
DrawMainMenu (
  VOID
  )
{
  DrawBackground ();
  DrawTitle (L"UEFINes");
  DrawListLine16 (80, gMainSel == 0, L"Browse ROM (FAT)");
  DrawListLine16 ((INT16)(80 + UI_MENU_ROW), gMainSel == 1, L"Controller");
  DrawListLine16 ((INT16)(80 + 2 * UI_MENU_ROW), gMainSel == 2, L"About");
  DrawListLine16 ((INT16)(80 + 3 * UI_MENU_ROW), gMainSel == 3, L"Exit to firmware");
  DrawFooter ();
}

STATIC
VOID
DrawVolPick (
  VOID
  )
{
  UINTN   I;
  CHAR16  Line[32];

  DrawBackground ();
  DrawTitle (L"Select volume");
  for (I = 0; I < FsVolumeCount (); I++) {
    UnicodeSPrint (Line, sizeof (Line), L"Volume %u", (UINT32)I);
    DrawListLine16 ((INT16)(80 + I * UI_MENU_ROW), gVolSel == (INTN)I, Line);
  }

  DrawFooter ();
}

STATIC
VOID
DrawBrowse (
  VOID
  )
{
  UINTN  I;
  UINTN  MaxVis;
  INTN   Start;
  INTN   Row;

  DrawBackground ();
  DrawTitle (L"Select ROM");
  MaxVis = (gKernel.graphics->Mode->Info->VerticalResolution - 160) / (UINT32)UI_MENU_ROW;
  if (MaxVis < 4) {
    MaxVis = 4;
  }

  Start = 0;
  if (gBrowseSel >= (INTN)MaxVis) {
    Start = gBrowseSel - (INTN)MaxVis + 1;
  }

  for (I = 0; I < MaxVis; I++) {
    Row = Start + (INTN)I;
    if (Row >= (INTN)gEntryCount) {
      break;
    }

    DrawListLine16 (
      (INT16)(80 + I * UI_MENU_ROW),
      gBrowseSel == Row,
      gEntries[Row].Name
      );
  }

  DrawFooter ();
}

STATIC
VOID
DrawSettings (
  VOID
  )
{
  CHAR16  Line[96];

  DrawBackground ();
  DrawTitle (L"Controller");
  UnicodeSPrint (
    Line,
    sizeof (Line),
    L"Hold: %u frames (Enter cycles)",
    (UINT32)g_settings_hold_frames
    );
  DrawListLine16 (80, gSettingsSel == 0, Line);
  if (g_settings_keymap == 0) {
    DrawListLine16 (
      (INT16)(80 + UI_MENU_ROW),
      gSettingsSel == 1,
      L"Keys: K J U I + WASD / arrows"
      );
  } else {
    DrawListLine16 (
      (INT16)(80 + UI_MENU_ROW),
      gSettingsSel == 1,
      L"Keys: Z X C V + arrows"
      );
  }

  DrawListLine16 ((INT16)(80 + 2 * UI_MENU_ROW), gSettingsSel == 2, L"Back");
  DrawFooter ();
}

STATIC
VOID
DrawAbout (
  VOID
  )
{
  DrawBackground ();
  DrawTitle (L"About");
  DrawListLine16 (80, FALSE, L"UEFINes / UEFI NES");
  DrawListLine16 ((INT16)(80 + UI_MENU_ROW), FALSE, L"Engine: NesUEFI (MIT)");
  DrawListLine16 ((INT16)(80 + 2 * UI_MENU_ROW), FALSE, L"Keys: see Controller menu");
  DrawListLine16((INT16)(80 + 3 * UI_MENU_ROW), FALSE, L"https://github.com/MobtgZhang/UEFINes");
  DrawFooter ();
}

STATIC
VOID
DrawErr (
  VOID
  )
{
  CHAR16  Wbuf[160];

  DrawBackground ();
  DrawTitle (L"Notice");
  AsciiStrToUnicodeStrS (gErrLine, Wbuf, sizeof (Wbuf) / sizeof (Wbuf[0]));
  DrawListLine16 (100, FALSE, Wbuf);
  DrawListLine16 ((INT16)(100 + UI_MENU_ROW), FALSE, L"Enter or Esc to continue");
}

VOID
EFIAPI
AppMenuInit (
  VOID
  )
{
  gView       = ViewMain;
  gMainSel    = 0;
  gBrowseSel  = 0;
  gVolSel     = 0;
  gSettingsSel = 0;
  mUiDirty    = TRUE;
}

BOOLEAN
EFIAPI
AppMenuDrawStaticUi (
  VOID
  )
{
  if (!mUiDirty) {
    return FALSE;
  }

  switch (gView) {
    case ViewMain:
      DrawMainMenu ();
      break;
    case ViewVolPick:
      DrawVolPick ();
      break;
    case ViewBrowse:
      DrawBrowse ();
      break;
    case ViewSettings:
      DrawSettings ();
      break;
    case ViewAbout:
      DrawAbout ();
      break;
    case ViewErr:
      DrawErr ();
      break;
    case ViewPlay:
    default:
      break;
  }

  mUiDirty = FALSE;
  return TRUE;
}

VOID
EFIAPI
AppMenuHandleInput (
  VOID
  )
{
  INTN  MainCount;
  INTN  SetCount;
  APP_VIEW     SnapView;
  INTN         SnapMainSel;
  INTN         SnapVolSel;
  INTN         SnapBrowseSel;
  INTN         SnapSettingsSel;
  UINTN        SnapEntryCount;
  UINTN        SnapPathDepth;
  UINT8        SnapHold;
  UINT32       SnapKeymap;
  CHAR8        SnapErr[sizeof (gErrLine)];

  SnapView        = gView;
  SnapMainSel     = gMainSel;
  SnapVolSel      = gVolSel;
  SnapBrowseSel   = gBrowseSel;
  SnapSettingsSel = gSettingsSel;
  SnapEntryCount  = gEntryCount;
  SnapPathDepth   = gPathDepth;
  SnapHold        = g_settings_hold_frames;
  SnapKeymap      = g_settings_keymap;
  AsciiStrCpyS (SnapErr, sizeof (SnapErr), gErrLine);

  MainCount = 4;
  SetCount  = 3;
  switch (gView) {
    case ViewMain:
      if (InputMenuUp ()) {
        if (gMainSel > 0) {
          gMainSel--;
        }
      } else if (InputMenuDown ()) {
        if (gMainSel < MainCount - 1) {
          gMainSel++;
        }
      } else if (InputMenuEnter ()) {
        switch (gMainSel) {
          case 0:
            EnterBrowseFromMain ();
            break;
          case 1:
            (VOID)SettingsLoad ();
            gView         = ViewSettings;
            gSettingsSel  = 0;
            break;
          case 2:
            gView = ViewAbout;
            break;
          case 3:
          default:
            gBS->Exit (gKernel.ImageHandle, EFI_SUCCESS, 0, NULL);
            break;
        }
      } else if (InputMenuEsc ()) {
      }

      break;

    case ViewVolPick:
      if (InputMenuUp ()) {
        if (gVolSel > 0) {
          gVolSel--;
        }
      } else if (InputMenuDown ()) {
        if (gVolSel < (INTN)FsVolumeCount () - 1) {
          gVolSel++;
        }
      } else if (InputMenuEnter ()) {
        if (OpenPickedVolume () == EFI_SUCCESS) {
          gPathDepth = 0;
          if (!EFI_ERROR (ReopenCwdFromStack ())) {
            gView       = ViewBrowse;
            gBrowseSel  = 0;
            RefreshBrowseList ();
            MaybeAutoEnterRomSubdir ();
          }
        }
      } else if (InputMenuEsc ()) {
        gView = ViewMain;
      }

      break;

    case ViewBrowse:
      if (InputMenuUp ()) {
        if (gBrowseSel > 0) {
          gBrowseSel--;
        }
      } else if (InputMenuDown ()) {
        if (gBrowseSel < (INTN)gEntryCount - 1) {
          gBrowseSel++;
        }
      } else if (InputMenuPgUp ()) {
        gBrowseSel -= 8;
        if (gBrowseSel < 0) {
          gBrowseSel = 0;
        }
      } else if (InputMenuPgDown ()) {
        gBrowseSel += 8;
        if (gBrowseSel >= (INTN)gEntryCount) {
          gBrowseSel = (INTN)gEntryCount - 1;
        }
      } else if (InputMenuEnter ()) {
        if (gEntryCount == 0) {
          break;
        }

        if (StrCmp (gEntries[gBrowseSel].Name, L"..") == 0) {
          GoParentDir ();
        } else if (gEntries[gBrowseSel].IsDir) {
          EnterSubdir (gEntries[gBrowseSel].Name);
        } else if (FsNameIsNesRom (gEntries[gBrowseSel].Name)) {
          TryLoadRom (gEntries[gBrowseSel].Name);
        }
      } else if (InputMenuEsc ()) {
        GoParentDir ();
      }

      break;

    case ViewSettings:
      if (InputMenuUp ()) {
        if (gSettingsSel > 0) {
          gSettingsSel--;
        }
      } else if (InputMenuDown ()) {
        if (gSettingsSel < SetCount - 1) {
          gSettingsSel++;
        }
      } else if (InputMenuEnter ()) {
        switch (gSettingsSel) {
          case 0:
            switch (g_settings_hold_frames) {
              case 5:
                g_settings_hold_frames = 10;
                break;
              case 10:
                g_settings_hold_frames = 15;
                break;
              case 15:
                g_settings_hold_frames = 20;
                break;
              default:
                g_settings_hold_frames = 5;
                break;
            }

            (VOID)SettingsSave ();
            break;
          case 1:
            g_settings_keymap = (g_settings_keymap == 0) ? 1U : 0U;
            (VOID)SettingsSave ();
            break;
          case 2:
          default:
            gView = ViewMain;
            break;
        }
      } else if (InputMenuEsc ()) {
        gView = ViewMain;
      }

      break;

    case ViewAbout:
      if (InputMenuEnter () || InputMenuEsc ()) {
        gView = ViewMain;
      }

      break;

    case ViewErr:
      if (InputMenuEnter () || InputMenuEsc ()) {
        gView = ViewBrowse;
        if ((gCwd == NULL) && (gVolRoot == NULL)) {
          gView = ViewMain;
        }
      }

      break;

    case ViewPlay:
      if (InputMenuEsc ()) {
        gView = ViewBrowse;
        graphics_clear_framebuffer (gKernel.graphics);
        RefreshBrowseList ();
      }

      break;

    default:
      break;
  }

  if ((gView != SnapView) || (gMainSel != SnapMainSel) || (gVolSel != SnapVolSel) ||
      (gBrowseSel != SnapBrowseSel) || (gSettingsSel != SnapSettingsSel) ||
      (gEntryCount != SnapEntryCount) || (gPathDepth != SnapPathDepth) ||
      (g_settings_hold_frames != SnapHold) || (g_settings_keymap != SnapKeymap) ||
      (AsciiStrCmp (gErrLine, SnapErr) != 0))
  {
    mUiDirty = TRUE;
  }
}

BOOLEAN
EFIAPI
AppMenuIsPlaying (
  VOID
  )
{
  return (BOOLEAN)(gView == ViewPlay);
}

VOID
EFIAPI
AppRunOneEmulatorFrame (
  VOID
  )
{
  fce_run ();
  nes_gfx_swap ();
}
