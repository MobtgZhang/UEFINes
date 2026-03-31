#ifndef TIMER_H_
#define TIMER_H_

#include <Uefi.h>

#define TICK_PER_SECOND  60

EFI_STATUS
EFIAPI
TimerInit (
  IN UINT32  TicksPerSecond
  );

UINT64
EFIAPI
TimerTicks (
  VOID
  );

#endif
