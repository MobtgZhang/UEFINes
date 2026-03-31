#include "ppu.h"
#include "ppu-internal.h"
#include "cpu.h"
#include "fce.h"
#include "memory.h"
#include "../Hal/nes_hal.h"
#include <Library/BaseMemoryLib.h>

byte  PPU_RAM[0x4000];
byte  PPU_SPRRAM[0x100];

byte ppu_sprite_palette[4][4];
/* $2007: non-palette reads return previous buffered byte (2C02 behavior). */
STATIC byte  ppu_2007_read_buffer;
byte ppu_addr_latch;



// PPUCTRL Functions

inline word ppu_base_nametable_address()                            { return ppu_base_nametable_addresses[ppu.PPUCTRL & 0x3];  }
inline byte ppu_vram_address_increment()                            { return common_bit_set(ppu.PPUCTRL, 2) ? 32 : 1;          }
inline word ppu_sprite_pattern_table_address()                      { return common_bit_set(ppu.PPUCTRL, 3) ? 0x1000 : 0x0000; }
inline word ppu_background_pattern_table_address()                  { return common_bit_set(ppu.PPUCTRL, 4) ? 0x1000 : 0x0000; }
inline byte ppu_sprite_height()                                     { return common_bit_set(ppu.PPUCTRL, 5) ? 16 : 8;          }
inline bool ppu_generates_nmi()                                     { return common_bit_set(ppu.PPUCTRL, 7);                   }



// PPUMASK Functions

inline bool ppu_renders_grayscale()                                 { return common_bit_set(ppu.PPUMASK, 0); }
inline bool ppu_shows_background_in_leftmost_8px()                  { return common_bit_set(ppu.PPUMASK, 1); }
inline bool ppu_shows_sprites_in_leftmost_8px()                     { return common_bit_set(ppu.PPUMASK, 2); }
inline bool ppu_shows_background()                                  { return common_bit_set(ppu.PPUMASK, 3); }
inline bool ppu_shows_sprites()                                     { return common_bit_set(ppu.PPUMASK, 4); }
inline bool ppu_intensifies_reds()                                  { return common_bit_set(ppu.PPUMASK, 5); }
inline bool ppu_intensifies_greens()                                { return common_bit_set(ppu.PPUMASK, 6); }
inline bool ppu_intensifies_blues()                                 { return common_bit_set(ppu.PPUMASK, 7); }

inline void ppu_set_renders_grayscale(bool yesno)                   { common_modify_bitb(&ppu.PPUMASK, 0, yesno); }
inline void ppu_set_shows_background_in_leftmost_8px(bool yesno)    { common_modify_bitb(&ppu.PPUMASK, 1, yesno); }
inline void ppu_set_shows_sprites_in_leftmost_8px(bool yesno)       { common_modify_bitb(&ppu.PPUMASK, 2, yesno); }
inline void ppu_set_shows_background(bool yesno)                    { common_modify_bitb(&ppu.PPUMASK, 3, yesno); }
inline void ppu_set_shows_sprites(bool yesno)                       { common_modify_bitb(&ppu.PPUMASK, 4, yesno); }
inline void ppu_set_intensifies_reds(bool yesno)                    { common_modify_bitb(&ppu.PPUMASK, 5, yesno); }
inline void ppu_set_intensifies_greens(bool yesno)                  { common_modify_bitb(&ppu.PPUMASK, 6, yesno); }
inline void ppu_set_intensifies_blues(bool yesno)                   { common_modify_bitb(&ppu.PPUMASK, 7, yesno); }



// PPUSTATUS Functions

inline bool ppu_sprite_overflow()                                   { return common_bit_set(ppu.PPUSTATUS, 5); }
inline bool ppu_sprite_0_hit()                                      { return common_bit_set(ppu.PPUSTATUS, 6); }
inline bool ppu_in_vblank()                                         { return common_bit_set(ppu.PPUSTATUS, 7); }

inline void ppu_set_sprite_overflow(bool yesno)                     { common_modify_bitb(&ppu.PPUSTATUS, 5, yesno); }
inline void ppu_set_sprite_0_hit(bool yesno)                        { common_modify_bitb(&ppu.PPUSTATUS, 6, yesno); }
inline void ppu_set_in_vblank(bool yesno)                           { common_modify_bitb(&ppu.PPUSTATUS, 7, yesno); }



// RAM

inline word ppu_get_real_ram_address(word address)
{
    if (address < 0x2000) {
        return address;
    }
    else if (address < 0x3F00) {
        if (address < 0x3000) {
            return address;
        }
        else {
            return (word)(address - 0x1000);
        }
    }
    else if (address < 0x4000) {
        address = 0x3F00 | (address & 0x1F);
        if (address == 0x3F10 || address == 0x3F14 || address == 0x3F18 || address == 0x3F1C)
            return address - 0x10;
        else
            return address;
    }
    return 0xFFFF;
}

STATIC
word
ppu_resolve_vram_addr (
  word  Addr
  )
{
  word  t;
  word  off;

  t = Addr & 0x3FFF;
  if (t < 0x2000) {
    return t;
  }

  if (t >= 0x3F00) {
    return ppu_get_real_ram_address (t);
  }

  if (t >= 0x3000) {
    t = (word)(t - 0x1000);
  }

  off = (word)(t - 0x2000);
  /*
    Match iNES flags6 bit0 and common emulators (FCEUX / nesdev "Mirroring"):
    - mirroring == 0: horizontal — $2000=$2800, $2400=$2C00 (fold by -0x800).
    - mirroring == 1: vertical   — $2000=$2400, $2800=$2C00 (fold right/bottom).
  */
  if (ppu.mirroring == 0) {
    if (off >= 0x800) {
      off = (word)(off - 0x800);
    }
  } else {
    if ((off >= 0x400) && (off < 0x800)) {
      off = (word)(off - 0x400);
    } else if (off >= 0xC00) {
      off = (word)(off - 0x400);
    }
  }

  return (word)(0x2000 + off);
}

inline byte ppu_ram_read(word address)
{
  word  idx;

  idx = ppu_resolve_vram_addr (address);
  if (idx == 0xFFFF) {
    return 0;
  }

  return PPU_RAM[idx];
}

inline void ppu_ram_write(word address, byte data)
{
  word  idx;

  idx = ppu_resolve_vram_addr (address);
  if (idx != 0xFFFF) {
    PPU_RAM[idx] = data;
  }
}


// 3F01 = 0F (00001111)
// 3F02 = 2A (00101010)
// 3F03 = 09 (00001001)
// 3F04 = 07 (00000111)
// 3F05 = 0F (00001111)
// 3F06 = 30 (00110000)
// 3F07 = 27 (00100111)
// 3F08 = 15 (00010101)
// 3F09 = 0F (00001111)
// 3F0A = 30 (00110000)
// 3F0B = 02 (00000010)
// 3F0C = 21 (00100001)
// 3F0D = 0F (00001111)
// 3F0E = 30 (00110000)
// 3F0F = 00 (00000000)
// 3F11 = 0F (00001111)
// 3F12 = 16 (00010110)
// 3F13 = 12 (00010010)
// 3F14 = 37 (00110111)
// 3F15 = 0F (00001111)
// 3F16 = 12 (00010010)
// 3F17 = 16 (00010110)
// 3F18 = 37 (00110111)
// 3F19 = 0F (00001111)
// 3F1A = 17 (00010111)
// 3F1B = 11 (00010001)
// 3F1C = 35 (00110101)
// 3F1D = 0F (00001111)
// 3F1E = 17 (00010111)
// 3F1F = 11 (00010001)
// 3F20 = 2B (00101011)


// Rendering

/*
  One pass over visible 256 columns. Applies $2005 scroll (X/Y) and nametable
  carries (+0x400 / +0x800) so tile, attribute, and pattern row stay aligned
  (LiteNES previously ignored PPUSCROLL_Y and used scanline>>5 for attrs).
*/
void
ppu_draw_background_scanline (
  void
  )
{
  unsigned  scroll_x;
  unsigned  scroll_y;
  unsigned  sl;
  unsigned  fy;
  word      nt_row_carry;
  unsigned  y_in_band;
  unsigned  tile_y;
  unsigned  y_in_tile;
  word      nt_base0;
  int       sx;
  int       sx_start;
  unsigned  cache_key;
  unsigned  cur_key;
  byte      l;
  byte      h;

  scroll_x = ppu.PPUSCROLL_X;
  scroll_y = ppu.PPUSCROLL_Y;
  sl       = (unsigned)ppu.scanline;
  fy       = scroll_y + sl;
  nt_row_carry = (fy >= 256) ? 0x800u : 0u;
  y_in_band    = fy & 255u;
  tile_y       = (y_in_band >> 3) & 31u;
  y_in_tile    = y_in_band & 7u;
  nt_base0     = ppu_base_nametable_address ();

  cache_key = 0xFFFFFFFFu;
  l         = 0;
  h         = 0;

  sx_start = ppu_shows_background_in_leftmost_8px () ? 0 : 8;

  for (sx = sx_start; sx < 256; sx++) {
    unsigned  fx;
    word      nt_col_carry;
    unsigned  x_in_band;
    unsigned  tile_x_nt;
    unsigned  fine_x;
    byte      color;

    fx             = (unsigned)sx + scroll_x;
    nt_col_carry   = (fx >= 256) ? 0x400u : 0u;
    x_in_band      = fx & 255u;
    tile_x_nt      = (x_in_band >> 3) & 31u;
    fine_x         = x_in_band & 7u;

    /*
      Must not OR nt_row_carry (0x800) into the same field as (tile_y<<8):
      0x800 == (8<<8), so different NT rows reused wrong pattern (vertical bands).
    */
    cur_key = (((unsigned)(nt_row_carry != 0) ? 1u : 0u) << 25) |
              (((unsigned)(nt_col_carry != 0) ? 1u : 0u) << 24) |
              (tile_y << 8) | tile_x_nt;
    if (cur_key != cache_key) {
      word  nt_addr;
      byte  tile_index;
      word  tile_address;

      cache_key = cur_key;
      nt_addr   = (word)(nt_base0 + nt_col_carry + nt_row_carry +
                         (word)(tile_y * 32 + tile_x_nt));
      tile_index   = ppu_ram_read (nt_addr);
      tile_address = (word)(ppu_background_pattern_table_address () +
                            (word)(16 * tile_index));
      l = ppu_ram_read ((word)(tile_address + (word)y_in_tile));
      h = ppu_ram_read ((word)(tile_address + (word)y_in_tile + 8));
    }

    color = ppu_l_h_addition_table[l][h][fine_x];
    if (color != 0) {
      word   attr_base;
      word   attribute_address;
      byte   palette_attribute;
      bool   top;
      bool   left;
      word   palette_address;
      int    idx;

      attr_base = (word)(nt_base0 + nt_col_carry + nt_row_carry + 0x3C0);
      attribute_address = (word)(attr_base + (word)(((tile_y >> 2) * 8u) +
                                                    (tile_x_nt >> 2)));
      palette_attribute = ppu_ram_read (attribute_address);
      top                 = (y_in_band & 16U) == 0;
      left                = (x_in_band & 16U) == 0;
      if (!top) {
        palette_attribute >>= 4;
      }

      if (!left) {
        palette_attribute >>= 2;
      }

      palette_attribute &= 3;
      palette_address = (word)(0x3F00 + (palette_attribute << 2));
      idx             = ppu_ram_read ((word)(palette_address + color));

      if ((sx >= 0) && (sx < 248) && ((int)sl < 264)) {
        ppu_screen_background[sx][sl] = color;
      }

      pixbuf_add (bg, sx, (int)sl + 1, idx);
    }
  }
}

void ppu_draw_sprite_scanline()
{
    int   scanline_sprite_count = 0;
    int   n;
    int   sl;
    byte  sph;
    int   top;
    int   y_in_sprite;
    word  tile_index;
    word  tile_address;
    int   y_in_tile;

    sl  = ppu.scanline;
    sph = ppu_sprite_height ();

    for (n = 0; n < 0x100; n += 4) {
        byte  sprite_y = PPU_SPRRAM[n];
        byte  sprite_x = PPU_SPRRAM[n + 3];
        byte  attr      = PPU_SPRRAM[n + 2];
        bool  vflip     = (attr & 0x80) != 0;
        bool  hflip     = (attr & 0x40) != 0;

        if (sprite_y >= 239) {
          continue;
        }

        /* OAM Y is one less than top scanline (2C02). */
        top = (int)sprite_y + 1;
        if ((sl < top) || (sl >= top + (int)sph)) {
          continue;
        }

        y_in_sprite = sl - top;

        scanline_sprite_count++;
        if (scanline_sprite_count > 8) {
            ppu_set_sprite_overflow(true);
        }

        tile_index = PPU_SPRRAM[n + 1];

        if (sph == 16) {
          unsigned  y_line;
          int       half;
          word      tpair;
          word      base;

          y_line   = (unsigned)(vflip ? (15 - y_in_sprite) : y_in_sprite);
          half     = (int)(y_line / 8U);
          y_in_tile = (int)(y_line % 8U);
          tpair    = (word)(tile_index & 0xFE);
          base     = (tile_index & 1) ? 0x1000 : 0;
          tile_address = (word)(base + 16 * (tpair + (word)half));
        } else {
          unsigned  y_line;

          y_line     = (unsigned)(vflip ? (7 - y_in_sprite) : y_in_sprite);
          y_in_tile  = (int)y_line;
          tile_address = (word)(ppu_sprite_pattern_table_address () + 16 * tile_index);
        }

        {
          byte  l;
          byte  h;
          byte  palette_attribute;
          word  palette_address;
          int   x;

          l = ppu_ram_read ((word)(tile_address + (word)y_in_tile));
          h = ppu_ram_read ((word)(tile_address + (word)y_in_tile + 8));
          palette_attribute = attr & 0x3;
          palette_address   = (word)(0x3F10 + (palette_attribute << 2));

          for (x = 0; x < 8; x++) {
            int  color;
            int  screen_x;
            int  idx;

            color = hflip ? ppu_l_h_addition_flip_table[l][h][x] : ppu_l_h_addition_table[l][h][x];
            if (color == 0) {
              continue;
            }

            screen_x = (int)sprite_x + x;
            idx      = ppu_ram_read ((word)(palette_address + (word)color));

            if ((attr & 0x20) != 0) {
              pixbuf_add (bbg, screen_x, sl + 1, idx);
            } else {
              pixbuf_add (fg, screen_x, sl + 1, idx);
            }

            if (ppu_shows_background () && !ppu_sprite_hit_occured && (n == 0) &&
                (screen_x >= 0) && (screen_x < 248) && (sl >= 0) && (sl < 264) &&
                (ppu_screen_background[screen_x][sl] == (byte)color))
            {
              ppu_set_sprite_0_hit (true);
              ppu_sprite_hit_occured = true;
            }
          }
        }
    }
}



// PPU Lifecycle

void ppu_run(int cycles)
{
    while (cycles-- > 0) {
        ppu_cycle();
    }
}

void ppu_cycle()
{
    if (!ppu.ready && cpu_clock() > 29658)
        ppu.ready = true;

    ppu.scanline++;
    if (ppu_shows_background()) {
        ppu_draw_background_scanline ();
    }
    
    if (ppu_shows_sprites()) ppu_draw_sprite_scanline();

    if (ppu.scanline == 241) {
        ppu_set_in_vblank(true);
        ppu_set_sprite_0_hit(false);
        cpu_interrupt();
    }
    else if (ppu.scanline == 262) {
        ppu.scanline = -1;
        ppu_sprite_hit_occured = false;
        ppu_set_in_vblank(false);
        fce_update_screen();
    }
}

inline void ppu_copy(word address, byte *source, int length)
{
    CopyMem (&PPU_RAM[address], source, (UINTN)length);
}

inline byte ppu_io_read(word address)
{
    ppu.PPUADDR &= 0x3FFF;
    switch (address & 7) {
        case 2:
        {
            byte value = ppu.PPUSTATUS;
            ppu_set_in_vblank(false);
            ppu_set_sprite_0_hit(false);
            ppu.scroll_received_x = 0;
            ppu.PPUSCROLL = 0;
            ppu.addr_received_high_byte = 0;
            ppu_latch = value;
            ppu_addr_latch = 0;
            ppu_2007_read_buffer = 0;
            return value;
        }
        case 4: return ppu_latch = PPU_SPRRAM[ppu.OAMADDR];
        case 7:
        {
            word  a;
            word  inc;
            byte  ret;

            a   = ppu.PPUADDR & 0x3FFF;
            inc = ppu_vram_address_increment ();
            if (a >= 0x3F00) {
              /* Palette: immediate read; buffer loads mirrored NT/pattern below. */
              ret = ppu_ram_read (a);
              ppu_2007_read_buffer = ppu_ram_read ((word)(a - 0x1000));
            } else {
              ret = ppu_2007_read_buffer;
              ppu_2007_read_buffer = ppu_ram_read (a);
            }

            ppu.PPUADDR = (word)((a + inc) & 0x3FFF);
            ppu_latch    = ret;
            return ret;
        }
        default:
            return 0xFF;
    }
}

inline void ppu_io_write(word address, byte data)
{
    address &= 7;
    ppu_latch = data;
    ppu.PPUADDR &= 0x3FFF;
    switch(address) {
        case 0: if (ppu.ready) ppu.PPUCTRL = data; break;
        case 1: if (ppu.ready) ppu.PPUMASK = data; break;
        case 3: ppu.OAMADDR = data; break;
        case 4: PPU_SPRRAM[ppu.OAMADDR++] = data; break;
        case 5:
        {
            if (ppu.scroll_received_x)
                ppu.PPUSCROLL_Y = data;
            else
                ppu.PPUSCROLL_X = data;

            ppu.scroll_received_x ^= 1;
            break;
        }
        case 6:
        {
            if (!ppu.ready)
                return;

            if (ppu.addr_received_high_byte) {
              ppu.PPUADDR = (ppu_addr_latch << 8) + data;
              ppu_2007_read_buffer = 0;
            } else {
              ppu_addr_latch = data;
            }

            ppu.addr_received_high_byte ^= 1;
            break;
        }
        case 7:
            ppu_ram_write (ppu.PPUADDR, data);
            ppu.PPUADDR += ppu_vram_address_increment ();
            break;
    }
    ppu_latch = data;
}

void ppu_init()
{
    ppu.PPUCTRL = ppu.PPUMASK = ppu.PPUSTATUS = ppu.OAMADDR = ppu.PPUSCROLL_X = ppu.PPUSCROLL_Y = ppu.PPUADDR = 0;
    ppu.scanline = -1;
    ppu.x        = 0;
    ppu.PPUSTATUS |= 0xA0;
    ppu.PPUDATA = 0;
    ppu_2007_read_buffer = 0;

    // Initializing low-high byte-pairs for pattern tables
    int h, l, x;
    for (h = 0; h < 0x100; h++) {
        for (l = 0; l < 0x100; l++) {
            for (x = 0; x < 8; x++) {
                ppu_l_h_addition_table[l][h][x] = (((h >> (7 - x)) & 1) << 1) | ((l >> (7 - x)) & 1);
                ppu_l_h_addition_flip_table[l][h][x] = (((h >> x) & 1) << 1) | ((l >> x) & 1);
            }
        }
    }
}

void ppu_sprram_write(byte data)
{
    PPU_SPRRAM[ppu.OAMADDR++] = data;
}

void ppu_set_background_color(byte color)
{
    nes_set_bg_color(color);
}

void ppu_set_mirroring(byte mirroring)
{
  ppu.mirroring = mirroring & 1;
}
