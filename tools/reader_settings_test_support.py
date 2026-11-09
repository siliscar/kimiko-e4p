"""中文：让旧页面抽取回归使用真实阅读设置模块，避免复制实现。
English: Let page-extraction regressions use the real reader-settings module without copied logic.
"""
from pathlib import Path
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
VIEW = ROOT / 'main/ui/ui_reader_settings.c'


def declarations():
    return (ROOT / 'main/ui/ui_reader_settings.h').read_text().replace('#pragma once', '').replace('#include "epdiy.h"', '')


def implementation():
    return VIEW.read_text().replace('#include "ui_reader_settings.h"', '').replace('#include "ui_kit.h"', '')


def geometry():
    return declarations() + '\n' + '\n'.join(function(name, VIEW) for name in (
        'settings_extra', 'ui_reader_settings_top', 'ui_reader_settings_rect')) + '\n'
