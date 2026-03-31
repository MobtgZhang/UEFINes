#include "fce.h"
#include "cpu.h"
#include "memory.h"
#include "ppu.h"
#include "../Hal/nes_hal.h"
#include "../Hal/nes.h"
#include "mmc.h"
#include <Library/BaseMemoryLib.h>

PixelBuf bg, bbg, fg;

static byte buf[1048576];

typedef struct {
    char signature[4];
    byte prg_block_count;
    byte chr_block_count;
    word rom_type;
    byte reserved[8];
} ines_header;

static ines_header fce_rom_header;

void
romread (
  char  *rom,
  void  *Dst,
  int   Size,
  int   Offset
  )
{
  CopyMem (Dst, rom + Offset, (UINTN)Size);
}

int
fce_load_rom (
  char  *rom
  )
{
  int     offset;
  int     prg_size;
  UINTN   chr_size;
  byte    flags6;
  word    rt;

  offset = 0;
  romread (rom, &fce_rom_header, sizeof (fce_rom_header), offset);
  offset += sizeof (ines_header);

  if (CompareMem (fce_rom_header.signature, "NES\x1A", 4) != 0) {
    return -1;
  }

  flags6 = (byte)(fce_rom_header.rom_type & 0xFF);
  if ((flags6 & 4) != 0) {
    offset += 512;
  }

  rt     = fce_rom_header.rom_type;
  mmc_id = (byte)(((rt & 0xF0) >> 4) | ((rt >> 8) & 0xF0));

  if ((mmc_id != 0) && (mmc_id != 1) && (mmc_id != 2) && (mmc_id != 3) &&
      (mmc_id != 4))
  {
    return -1;
  }

  prg_size = fce_rom_header.prg_block_count * 0x4000;
  romread (rom, buf, prg_size, offset);
  offset += prg_size;

  if (fce_rom_header.chr_block_count == 0) {
    chr_size = 0x2000;
    SetMem (buf + prg_size, 0x2000, 0);
    mmc_store_rom (buf, (UINTN)prg_size, buf + prg_size, chr_size);
  } else {
    chr_size = (UINTN)fce_rom_header.chr_block_count * 0x2000;
    romread (rom, buf + prg_size, (int)chr_size, offset);
    mmc_store_rom (buf, (UINTN)prg_size, buf + prg_size, chr_size);
  }

  mmc_reset_mapper_state ();

  if (mmc_setup_from_ines (
        mmc_id,
        fce_rom_header.prg_block_count,
        fce_rom_header.chr_block_count,
        (BOOLEAN)(fce_rom_header.chr_block_count == 0),
        flags6 & 1
        ) != 0)
  {
    return -1;
  }

  return 0;
}

void
fce_init (
  void
  )
{
  nes_hal_init ();
  cpu_init ();
  ppu_init ();
  /* Fresh CIRAM nametable/attr; avoids garbage between ROM loads. */
  SetMem (PPU_RAM + 0x2000, 0x1000, 0);
  cpu_reset ();
}

void
fce_run (
  void
  )
{
  int  scanlines;

  scanlines = 262;
  while (scanlines-- > 0) {
    ppu_run (1);
    cpu_run (1364 / 12);
  }
}

void
fce_update_screen (
  void
  )
{
  int  idx;
  int  i;

  idx = ppu_ram_read (0x3F00);
  nes_set_bg_color (idx);

  if (ppu_shows_sprites ()) {
    for (i = 0; i < bbg.size; i++) {
      nes_draw_pixel (bbg.buf + i);
    }
  }

  if (ppu_shows_background ()) {
    for (i = 0; i < bg.size; i++) {
      nes_draw_pixel (bg.buf + i);
    }
  }

  if (ppu_shows_sprites ()) {
    for (i = 0; i < fg.size; i++) {
      nes_draw_pixel (fg.buf + i);
    }
  }

  pixbuf_clean (bbg);
  pixbuf_clean (bg);
  pixbuf_clean (fg);
}
