#include "psg.h"
#include "../Hal/nes_hal.h"
#include "key.h"

static byte prev_write;
static int joy1_shift = 8;

inline byte psg_io_read(word address)
{
    // Joystick 1: one bit per read after strobe (A..Right = keys 1..8).
    if (address == 0x4016) {
        byte  v;

        if (joy1_shift < 8) {
            v = nes_get_key(joy1_shift + 1) ? 1 : 0;
        } else {
            v = 0;
        }

        joy1_shift++;
        return v;
    }

    return 0;
}

inline void psg_io_write(word address, byte data)
{
    if (address == 0x4016) {
        if ((data & 1) == 0 && prev_write == 1) {
            joy1_shift = 0;
        }
    }
    prev_write = data & 1;
}
