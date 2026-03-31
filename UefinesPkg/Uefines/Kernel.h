/** @file
  Global application context (NesUEFI-style naming for HAL compatibility).
**/
#ifndef KERNEL_H_
#define KERNEL_H_

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>

typedef struct {
  EFI_HANDLE                         ImageHandle;
  EFI_GRAPHICS_OUTPUT_PROTOCOL       *graphics;
  UINT64                             Ticks;
} KERNEL_CONTEXT;

extern KERNEL_CONTEXT  gKernel;

#define kernel  gKernel

#endif
