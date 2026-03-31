/** @file
  UEFI console input: menu navigation + short hold window for NES buttons (no PS/2).
**/
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "Input.h"

extern UINT8   g_settings_hold_frames;
extern UINT32  g_settings_keymap;

STATIC BOOLEAN  mMenuUp;
STATIC BOOLEAN  mMenuDown;
STATIC BOOLEAN  mMenuEnter;
STATIC BOOLEAN  mMenuEsc;
STATIC BOOLEAN  mMenuPgUp;
STATIC BOOLEAN  mMenuPgDown;

STATIC UINT8  mHoldA;
STATIC UINT8  mHoldB;
STATIC UINT8  mHoldSelect;
STATIC UINT8  mHoldStart;
STATIC UINT8  mHoldUp;
STATIC UINT8  mHoldDown;
STATIC UINT8  mHoldLeft;
STATIC UINT8  mHoldRight;

VOID
EFIAPI
InputInit (
  VOID
  )
{
  if (gST->ConIn != NULL) {
    (VOID)gST->ConIn->Reset (gST->ConIn, FALSE);
  }

  mMenuUp       = FALSE;
  mMenuDown     = FALSE;
  mMenuEnter    = FALSE;
  mMenuEsc      = FALSE;
  mMenuPgUp     = FALSE;
  mMenuPgDown   = FALSE;
  mHoldA        = 0;
  mHoldB        = 0;
  mHoldSelect   = 0;
  mHoldStart    = 0;
  mHoldUp       = 0;
  mHoldDown     = 0;
  mHoldLeft     = 0;
  mHoldRight    = 0;
}

STATIC
VOID
ArmHold (
  IN UINT8  *Counter
  )
{
  UINT8  n;

  n = g_settings_hold_frames;
  if ((n != 5) && (n != 10) && (n != 15) && (n != 20)) {
    n = 10;
  }

  *Counter = n;
}

STATIC VOID
ArmDpadPulse (
  IN UINT8  *Counter
  )
{
  *Counter = 1;
}

VOID
EFIAPI
InputPoll (
  VOID
  )
{
  EFI_STATUS    Status;
  EFI_INPUT_KEY Key;

  mMenuUp     = FALSE;
  mMenuDown   = FALSE;
  mMenuEnter  = FALSE;
  mMenuEsc    = FALSE;
  mMenuPgUp   = FALSE;
  mMenuPgDown = FALSE;

  for ( ; ; ) {
    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (Status == EFI_NOT_READY) {
      break;
    }

    if (EFI_ERROR (Status)) {
      break;
    }

    if (Key.ScanCode == SCAN_UP) {
      mMenuUp = TRUE;
      ArmDpadPulse (&mHoldUp);
    } else if (Key.ScanCode == SCAN_DOWN) {
      mMenuDown = TRUE;
      ArmDpadPulse (&mHoldDown);
    } else if (Key.ScanCode == SCAN_LEFT) {
      ArmDpadPulse (&mHoldLeft);
    } else if (Key.ScanCode == SCAN_RIGHT) {
      ArmDpadPulse (&mHoldRight);
    } else if (Key.ScanCode == SCAN_ESC) {
      mMenuEsc = TRUE;
    } else if (Key.ScanCode == SCAN_PAGE_UP) {
      mMenuPgUp = TRUE;
    } else if (Key.ScanCode == SCAN_PAGE_DOWN) {
      mMenuPgDown = TRUE;
    }

    if ((Key.UnicodeChar == CHAR_CARRIAGE_RETURN) ||
        (Key.UnicodeChar == CHAR_LINEFEED))
    {
      mMenuEnter = TRUE;
      ArmHold (&mHoldStart);
    }

    if (g_settings_keymap == 0) {
      switch (Key.UnicodeChar) {
        case L'k':
        case L'K':
          ArmHold (&mHoldA);
          break;
        case L'j':
        case L'J':
          ArmHold (&mHoldB);
          break;
        case L'u':
        case L'U':
          ArmHold (&mHoldSelect);
          break;
        case L'i':
        case L'I':
          ArmHold (&mHoldStart);
          break;
        case L'w':
        case L'W':
          ArmDpadPulse (&mHoldUp);
          break;
        case L's':
        case L'S':
          ArmDpadPulse (&mHoldDown);
          break;
        case L'a':
        case L'A':
          if (Key.ScanCode == SCAN_NULL) {
            ArmDpadPulse (&mHoldLeft);
          }
          break;
        case L'd':
        case L'D':
          ArmDpadPulse (&mHoldRight);
          break;
        default:
          break;
      }
    } else {
      switch (Key.UnicodeChar) {
        case L'z':
        case L'Z':
          ArmHold (&mHoldA);
          break;
        case L'x':
        case L'X':
          ArmHold (&mHoldB);
          break;
        case L'c':
        case L'C':
          ArmHold (&mHoldSelect);
          break;
        case L'v':
        case L'V':
          ArmHold (&mHoldStart);
          break;
        default:
          break;
      }
    }
  }
}

VOID
EFIAPI
InputEndFrame (
  VOID
  )
{
  if (mHoldA > 0) {
    mHoldA--;
  }

  if (mHoldB > 0) {
    mHoldB--;
  }

  if (mHoldSelect > 0) {
    mHoldSelect--;
  }

  if (mHoldStart > 0) {
    mHoldStart--;
  }

  if (mHoldUp > 0) {
    mHoldUp--;
  }

  if (mHoldDown > 0) {
    mHoldDown--;
  }

  if (mHoldLeft > 0) {
    mHoldLeft--;
  }

  if (mHoldRight > 0) {
    mHoldRight--;
  }
}

BOOLEAN
EFIAPI
InputMenuUp (
  VOID
  )
{
  return mMenuUp;
}

BOOLEAN
EFIAPI
InputMenuDown (
  VOID
  )
{
  return mMenuDown;
}

BOOLEAN
EFIAPI
InputMenuEnter (
  VOID
  )
{
  return mMenuEnter;
}

BOOLEAN
EFIAPI
InputMenuEsc (
  VOID
  )
{
  return mMenuEsc;
}

BOOLEAN
EFIAPI
InputMenuPgUp (
  VOID
  )
{
  return mMenuPgUp;
}

BOOLEAN
EFIAPI
InputMenuPgDown (
  VOID
  )
{
  return mMenuPgDown;
}

int
hal_nes_get_key (
  UINT16  Key
  )
{
  switch (Key) {
    case 0:
      return 1;
    case 1:
      return (mHoldA > 0) ? 1 : 0;
    case 2:
      return (mHoldB > 0) ? 1 : 0;
    case 3:
      return (mHoldSelect > 0) ? 1 : 0;
    case 4:
      return (mHoldStart > 0) ? 1 : 0;
    case 5:
      return (mHoldUp > 0) ? 1 : 0;
    case 6:
      return (mHoldDown > 0) ? 1 : 0;
    case 7:
      return (mHoldLeft > 0) ? 1 : 0;
    case 8:
      return (mHoldRight > 0) ? 1 : 0;
    default:
      return 0;
  }
}
