"""E4P 板释放像素驱动时保持 VCOM_CTRL。/ E4P board keeps VCOM_CTRL while releasing pixel drive.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'components/board_support/board_metalio_e4p.c').read_text()


def function(name):
    m = re.search(r'^(?:static )?[^\n]+\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    assert m, name
    at, depth = m.end(), 1
    while depth:
        if source[at] == '{':
            depth += 1
        elif source[at] == '}':
            depth -= 1
        at += 1
    return source[m.start():at]


unit = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct { bool ep_mode, ep_output_enable; } epd_ctrl_state_t;
static bool s_rails_on = true;
static bool s_vcom_ctrl = true;
static unsigned p0_writes;
static unsigned ctrl_calls;
static epd_ctrl_state_t state;

static void p0_write(void) { ++p0_writes; }
static epd_ctrl_state_t *epd_ctrl_state(void) { return &state; }
#define EPD_PIN_MODE 19
#define EPD_PIN_XOE 18
static void gpio_set_level(int pin, int level) {
    assert((pin==18 || pin==19) && !level); ++ctrl_calls;
}

"""
unit += function('board_epd_release_drive') + '\n'
unit += r"""
int main(void) {
    (void)p0_write;
    assert(board_epd_release_drive());
    assert(ctrl_calls == 2);
    assert(!state.ep_mode && !state.ep_output_enable);
    // 释放像素驱动不能撤掉 VCOM_CTRL，否则同一 burst 内后续扫描没有公共电压。
    // Releasing pixel drive must not drop VCOM_CTRL, or later scans in the same burst lose the common voltage.
    assert(s_vcom_ctrl);
    assert(p0_writes == 0);
    s_rails_on = false;
    assert(board_epd_release_drive());
    assert(ctrl_calls == 2);
    puts("PASS: E4P release_drive keeps VCOM_CTRL on while the rails are powered");
}
"""

with tempfile.TemporaryDirectory() as folder:
    c, binary = Path(folder) / 'test.c', Path(folder) / 'test'
    c.write_text(unit)
    flags = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror']
    if not os.environ.get('HOST_TEST_NO_SANITIZE'):
        flags.append('-fsanitize=address,undefined')
    subprocess.run(flags + [str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
