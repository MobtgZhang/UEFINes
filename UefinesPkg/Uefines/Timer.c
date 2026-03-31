#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "Kernel.h"
#include "Timer.h"

STATIC EFI_EVENT  mTimerEvent = NULL;

STATIC
VOID
EFIAPI
TimerNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  gKernel.Ticks++;
}

EFI_STATUS
EFIAPI
TimerInit (
  IN UINT32  TicksPerSecond
  )
{
  EFI_STATUS  Status;

  gKernel.Ticks = 0;
  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  TimerNotify,
                  NULL,
                  &mTimerEvent
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->SetTimer (
                  mTimerEvent,
                  TimerPeriodic,
                  DivU64x32 (10000000, TicksPerSecond)
                  );
  return Status;
}

UINT64
EFIAPI
TimerTicks (
  VOID
  )
{
  return gKernel.Ticks;
}
