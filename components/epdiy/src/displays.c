#include <stddef.h>

#include "epd_display.h"

// ED047TC2_1216 / 684×1216 40pin。Metalio 板走 8-bit 并口，20 MHz。
// ED047TC2_1216 / 684×1216 40-pin. The Metalio board uses an 8-bit bus at 20 MHz.
// 波形在应用侧 epd_hl_init(&E0470_WAVEFORM)，这里不挂社区 LUT。
const EpdDisplay_t E0470_DISPLAY = {
    .width = 1216,
    .height = 684,
    .bus_width = 8,
    .bus_speed = 20,
    .default_waveform = NULL,
};
