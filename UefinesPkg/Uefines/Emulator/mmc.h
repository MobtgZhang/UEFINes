#ifndef MMC_H_
#define MMC_H_

#include "common.h"
#include <Uefi.h>

extern byte  mmc_id;

byte mmc_read(word address);
void mmc_write(word address, byte data);
void mmc_copy(word address, byte *source, int length);
void mmc_append_chr_rom_page(byte *source);

void mmc_store_rom (
  IN byte   *Prg,
  IN UINTN  PrgLen,
  IN byte   *Chr,
  IN UINTN  ChrLen
  );

void mmc_reset_mapper_state (
  VOID
  );

int
mmc_setup_from_ines (
  IN byte      Mapper,
  IN byte      Prg16kBlocks,
  IN byte      Chr8kBanks,
  IN BOOLEAN   ChrRamOnly,
  IN byte      InesMirrorBit
  );

#endif