#!/usr/bin/env python3
"""中文：链接真实设置绘制模块并与提取前的指令摘要对照；可指定旧源码复核。
English: Link the real settings view against its pre-extraction drawing digest; optionally verify a legacy source file.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
# 334e224的3136组状态：两面板的文字、坐标、颜色、图标及绘制顺序，另含无效矩形与开关。
# 3136 states from 334e224: text, geometry, colors, icons and call order for both sheets, plus invalid rectangles and switches.
# 八项设置与晃动开关两态的绘制基准。/ Drawing baseline for eight settings and both shake toggle states.
LEGACY_EXPECTED = 'e7505b2b4d4f68496928912eb860e0f127fe3b926351886d3f3bbb6a3e116625'
LEGACY_LAYOUT_EXPECTED = '29a5b99acf14978497595b0cea28facb3187a8c73994ada200c3f811204304cf'
EXPECTED = '30497fb09bdcad1855480d7ba18ccbc371cbe674f897c7ff5ad1f19dba17a9a5'
LAYOUT_EXPECTED = '7e890b9d96711da49522f6d48b929b767ad65bca0a6ef3343620e1d7555b5085'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path)
    args = parser.parse_args()
    source = args.reference or ROOT / 'main/apps/app_book.c'
    legacy = bool(args.reference)
    names = ['reading_setting_rect', 'reading_settings_top', 'draw_reading_switch',
             'draw_reading_settings', 'draw_turn_settings']
    if legacy:
        names = ['reader_ui_extra', 'reading_setting_rect', 'reading_settings_top',
                 'draw_reading_switch', 'reader_panel_text_vc', 'draw_reading_card',
                 'draw_reading_toggle', 'draw_reading_settings', 'draw_turn_settings']
    with tempfile.TemporaryDirectory(prefix='reader-settings-') as directory:
        folder = Path(directory)
        (folder / 'page_adapter.inc').write_text('\n'.join(function(name, source) for name in names))
        compiler = shlex.split(os.environ.get('CC', 'cc'))
        command = compiler + ['-std=gnu11', '-Wall', '-Wextra', '-Werror',
                             '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                             '-I' + str(folder), '-I' + str(ROOT / 'tools/reader_settings_stubs'),
                             '-I' + str(ROOT / 'main/ui'), '-I' + str(ROOT / 'main/font'),
                             str(ROOT / 'tools/reader_settings_view_host_test.c')]
        command += ['-DLEGACY'] if legacy else [str(ROOT / 'main/ui/ui_reader_settings.c')]
        command += ['-o', str(folder / 'test')]
        subprocess.run(command, check=True)
        output = subprocess.check_output([str(folder / 'test')])
        digest = hashlib.sha256(output).hexdigest()
        print(f'reader settings: {len(output.splitlines())} drawing records, SHA256 {digest}')
        # 独立保留原布局、文字与顺序，只排除绘图颜色末字段。
        # Independently retain original geometry, text and order, excluding only drawing colors.
        colored = {b'round', b'frame', b'rect', b'icon', b'hairline', b'fill_circle', b'circle'}
        layout = b'\n'.join(line.rsplit(b' ', 1)[0] if line.split(b' ', 1)[0] in colored
                            else line for line in output.splitlines()) + b'\n'
        layout_digest = hashlib.sha256(layout).hexdigest()
        print(f'reader settings layout: SHA256 {layout_digest}')
        assert digest == (LEGACY_EXPECTED if legacy else EXPECTED), f'drawing order or values changed: {digest}'
        assert layout_digest == (LEGACY_LAYOUT_EXPECTED if legacy else LAYOUT_EXPECTED)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
