#include "mmc.h"
#include "ppu.h"
#include <Library/BaseMemoryLib.h>

#define MMC_MAX_PAGE_COUNT  256
#define MMC_PRG_MAX         0x80000
#define MMC_CHR_MAX         0x80000

byte    mmc_id;
byte    mmc_prg_pages[MMC_MAX_PAGE_COUNT][0x4000];
byte    mmc_chr_pages[MMC_MAX_PAGE_COUNT][0x2000];
int     mmc_prg_pages_number;
int     mmc_chr_pages_number;

byte    memory[0x10000];

STATIC byte     mmc_prg_rom[MMC_PRG_MAX];
STATIC UINTN    mmc_prg_len;
STATIC byte     mmc_chr_rom[MMC_CHR_MAX];
STATIC UINTN    mmc_chr_len;

/* MMC1 */
STATIC byte   mmc1_sr;
STATIC UINTN  mmc1_bit_count;
STATIC byte   mmc1_r[4];

/* MMC3 */
STATIC byte   mmc3_r[8];
STATIC byte   mmc3_8000;

STATIC void
MmcCopyPrg8k (
  IN word   CpuAddr,
  IN UINTN  Bank8k
  )
{
  UINTN  n8;
  UINTN  off;

  n8 = mmc_prg_len / 0x2000;
  if (n8 == 0) {
    return;
  }

  off = Bank8k % n8;
  CopyMem (&memory[CpuAddr], mmc_prg_rom + off * 0x2000, 0x2000);
}

void
MmcUxRom16 (
  IN byte  BankReg
  )
{
  UINTN  n16;
  UINTN  sel;
  UINTN  last;

  n16 = mmc_prg_len / 0x4000;
  if (n16 < 2) {
    return;
  }

  last = n16 - 1;
  sel  = (UINTN)BankReg % last;
  CopyMem (&memory[0x8000], mmc_prg_rom + sel * 0x4000, 0x4000);
  CopyMem (&memory[0xC000], mmc_prg_rom + last * 0x4000, 0x4000);
}

STATIC void
Mmc1Sync (
  VOID
  )
{
  UINTN  prg16;
  UINTN  b32;
  byte   c;
  byte   prg;
  byte   chr0;
  byte   chr1;
  UINTN  nk;

  prg16 = mmc_prg_len / 0x4000;
  if (prg16 == 0) {
    return;
  }

  c    = mmc1_r[0];
  prg  = mmc1_r[3];
  chr0 = mmc1_r[1];
  chr1 = mmc1_r[2];

  switch (c & 3) {
    case 2:
      ppu_set_mirroring (1);
      break;
    case 3:
      ppu_set_mirroring (0);
      break;
    default:
      ppu_set_mirroring (0);
      break;
  }

  if ((c & 0x08) == 0) {
    b32 = (UINTN)(prg >> 1) % prg16;
    CopyMem (&memory[0x8000], mmc_prg_rom + b32 * 0x4000, 0x8000);
  } else if ((c & 0x04) == 0) {
    CopyMem (
      &memory[0xC000],
      mmc_prg_rom + ((prg16 - 1) * 0x4000),
      0x4000
      );
    CopyMem (
      &memory[0x8000],
      mmc_prg_rom + (((UINTN)prg % prg16) * 0x4000),
      0x4000
      );
  } else {
    CopyMem (&memory[0x8000], mmc_prg_rom, 0x4000);
    CopyMem (
      &memory[0xC000],
      mmc_prg_rom + (((UINTN)prg % prg16) * 0x4000),
      0x4000
      );
  }

  if (mmc_chr_len == 0) {
    return;
  }

  nk = mmc_chr_len / 0x1000;
  if (nk == 0) {
    return;
  }

  if ((c & 0x10) == 0) {
    ppu_copy (0x0000, mmc_chr_rom + ((((UINTN)(chr0 & 0x1E)) % nk) * 0x1000), 0x2000);
  } else {
    ppu_copy (0x0000, mmc_chr_rom + (((UINTN)chr0 % nk) * 0x1000), 0x1000);
    ppu_copy (0x1000, mmc_chr_rom + (((UINTN)chr1 % nk) * 0x1000), 0x1000);
  }
}

STATIC void
Mmc1Write (
  IN word  Address,
  IN byte  Data
  )
{
  UINTN  reg;

  if (Data & 0x80) {
    mmc1_sr         = 0;
    mmc1_bit_count  = 0;
    mmc1_r[0]      |= 0x0C;
    Mmc1Sync ();
    return;
  }

  mmc1_sr |= (Data & 1) << mmc1_bit_count;
  mmc1_bit_count++;
  if (mmc1_bit_count < 5) {
    return;
  }

  reg = (UINTN)((Address - 0x8000) >> 13);
  if (reg > 3) {
    reg = 3;
  }

  mmc1_r[reg] = mmc1_sr & 0x1F;
  mmc1_sr         = 0;
  mmc1_bit_count  = 0;
  Mmc1Sync ();
}

STATIC void
Mmc3Chr1k (
  IN word  PpuOff,
  IN byte  Bank1k
  )
{
  UINTN  nb;
  UINTN  off;

  if (mmc_chr_len == 0) {
    return;
  }

  nb  = mmc_chr_len / 0x400;
  if (nb == 0) {
    return;
  }

  off = ((UINTN)Bank1k % nb) * 0x400;
  ppu_copy (PpuOff, mmc_chr_rom + off, 0x400);
}

STATIC void
Mmc3Sync (
  VOID
  )
{
  UINTN  prg8;
  UINTN  last8;
  UINTN  prelast8;
  byte   m;

  prg8 = mmc_prg_len / 0x2000;
  if (prg8 < 2) {
    return;
  }

  last8    = prg8 - 1;
  prelast8 = prg8 - 2;
  m        = mmc3_8000;
  if ((m & 0x40) == 0) {
    MmcCopyPrg8k (0x8000, mmc3_r[6]);
    MmcCopyPrg8k (0xA000, mmc3_r[7]);
    CopyMem (&memory[0xC000], mmc_prg_rom + prelast8 * 0x2000, 0x2000);
    CopyMem (&memory[0xE000], mmc_prg_rom + last8 * 0x2000, 0x2000);
  } else {
    CopyMem (&memory[0x8000], mmc_prg_rom + prelast8 * 0x2000, 0x2000);
    MmcCopyPrg8k (0xA000, mmc3_r[7]);
    MmcCopyPrg8k (0xC000, mmc3_r[6]);
    CopyMem (&memory[0xE000], mmc_prg_rom + last8 * 0x2000, 0x2000);
  }

  if ((m & 0x80) == 0) {
    Mmc3Chr1k (0x0000, mmc3_r[0]);
    Mmc3Chr1k (0x0400, mmc3_r[1]);
    Mmc3Chr1k (0x1000, mmc3_r[2]);
    Mmc3Chr1k (0x1400, mmc3_r[3]);
    Mmc3Chr1k (0x1800, mmc3_r[4]);
    Mmc3Chr1k (0x1C00, mmc3_r[5]);
  } else {
    Mmc3Chr1k (0x1000, mmc3_r[0]);
    Mmc3Chr1k (0x1400, mmc3_r[1]);
    Mmc3Chr1k (0x0000, mmc3_r[2]);
    Mmc3Chr1k (0x0400, mmc3_r[3]);
    Mmc3Chr1k (0x0800, mmc3_r[4]);
    Mmc3Chr1k (0x0C00, mmc3_r[5]);
  }
}

STATIC void
Mmc3Write (
  IN word  Address,
  IN byte  Data
  )
{
  switch (Address & 0xE001) {
    case 0x8000:
      mmc3_8000 = Data;
      break;
    case 0x8001:
      mmc3_r[mmc3_8000 & 7] = Data;
      Mmc3Sync ();
      break;
    case 0xA000:
      if (Data & 1) {
        ppu_set_mirroring (0);
      } else {
        ppu_set_mirroring (1);
      }

      break;
    default:
      break;
  }
}

VOID
mmc_store_rom (
  IN byte   *Prg,
  IN UINTN  PrgLen,
  IN byte   *Chr,
  IN UINTN  ChrLen
  )
{
  if (PrgLen > MMC_PRG_MAX) {
    PrgLen = MMC_PRG_MAX;
  }

  if (ChrLen > MMC_CHR_MAX) {
    ChrLen = MMC_CHR_MAX;
  }

  CopyMem (mmc_prg_rom, Prg, PrgLen);
  mmc_prg_len = PrgLen;
  if ((Chr != NULL) && (ChrLen > 0)) {
    CopyMem (mmc_chr_rom, Chr, ChrLen);
    mmc_chr_len = ChrLen;
  } else {
    mmc_chr_len = 0;
  }
}

VOID
mmc_reset_mapper_state (
  VOID
  )
{
  mmc1_sr         = 0;
  mmc1_bit_count  = 0;
  mmc1_r[0]       = 0x0C;
  mmc1_r[1]       = 0;
  mmc1_r[2]       = 0;
  mmc1_r[3]       = 0;
  mmc3_8000       = 0;
  SetMem (mmc3_r, sizeof (mmc3_r), 0);
}

inline byte
mmc_read (
  word  address
  )
{
  return memory[address];
}

inline void
mmc_write (
  word  address,
  byte  data
  )
{
  switch (mmc_id) {
    case 1:
      Mmc1Write (address, data);
      return;
    case 2:
      if (address >= 0x8000) {
        MmcUxRom16 (data);
      }

      return;
    case 3:
      ppu_copy (0x0000, &mmc_chr_pages[data & 3][0], 0x2000);
      return;
    case 4:
      Mmc3Write (address, data);
      return;
    default:
      break;
  }

  if (mmc_id == 0) {
    return;
  }

  memory[address] = data;
}

inline void
mmc_copy (
  word  address,
  byte  *source,
  int   length
  )
{
  CopyMem (&memory[address], source, (UINTN)length);
}

inline void
mmc_append_chr_rom_page (
  byte  *source
  )
{
  CopyMem (&mmc_chr_pages[mmc_chr_pages_number++][0], source, 0x2000);
}

int
mmc_setup_from_ines (
  IN byte      Mapper,
  IN byte      Prg16kBlocks,
  IN byte      Chr8kBanks,
  IN BOOLEAN   ChrRamOnly,
  IN byte      InesMirrorBit
  )
{
  UINTN  i;

  mmc_prg_pages_number  = 0;
  mmc_chr_pages_number  = 0;

  switch (Mapper) {
    case 0:
      ppu_set_mirroring (InesMirrorBit & 1);
      if (Prg16kBlocks <= 1) {
        mmc_copy (0x8000, mmc_prg_rom, 0x4000);
        mmc_copy (0xC000, mmc_prg_rom, 0x4000);
      } else {
        mmc_copy (0x8000, mmc_prg_rom, 0x8000);
      }

      if (ChrRamOnly || (Chr8kBanks == 0)) {
        ppu_copy (0x0000, mmc_chr_rom, 0x2000);
      } else {
        for (i = 0; i < (UINTN)Chr8kBanks; i++) {
          mmc_append_chr_rom_page (mmc_chr_rom + i * 0x2000);
        }

        ppu_copy (0x0000, mmc_chr_pages[0], 0x2000);
      }

      return 0;

    case 3:
      ppu_set_mirroring (InesMirrorBit & 1);
      if (Prg16kBlocks <= 1) {
        mmc_copy (0x8000, mmc_prg_rom, 0x4000);
        mmc_copy (0xC000, mmc_prg_rom, 0x4000);
      } else {
        mmc_copy (0x8000, mmc_prg_rom, 0x8000);
      }

      for (i = 0; i < (UINTN)Chr8kBanks; i++) {
        mmc_append_chr_rom_page (mmc_chr_rom + i * 0x2000);
      }

      ppu_copy (0x0000, mmc_chr_pages[0], 0x2000);
      return 0;

    case 2:
      ppu_set_mirroring (InesMirrorBit & 1);
      MmcUxRom16 (0);
      ppu_copy (0x0000, mmc_chr_rom, 0x2000);
      return 0;

    case 1:
      Mmc1Sync ();
      return 0;

    case 4:
      /*
        MMC3 power-on: all-zero bank regs map every 1K CHR slot to bank 0 (garbled
        repeating tiles) and duplicate PRG at $8000/$A000. Match common emu defaults.
      */
      ppu_set_mirroring (InesMirrorBit & 1);
      mmc3_8000 = 0;
      mmc3_r[0] = 0;
      mmc3_r[1] = 1;
      mmc3_r[2] = 2;
      mmc3_r[3] = 3;
      mmc3_r[4] = 4;
      mmc3_r[5] = 5;
      mmc3_r[6] = 0;
      mmc3_r[7] = 1;
      Mmc3Sync ();
      return 0;

    default:
      return -1;
  }
}
