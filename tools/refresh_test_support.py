"""中文：抽取页面测试的薄显示边界；实际策略和分发由 refresh 专项穷举验证。
English: Thin display boundary for extracted-page tests; the refresh suite exhaustively checks real policy/dispatch.
"""
from pathlib import Path


def area_boundary(root: Path, *, detail_only=False, feedback_only=False, main_clock=False):
    # 使用真实公开意图，避免测试枚举与产品漂移。/ Use real public intents so test enums cannot drift from product code.
    header = (root / "main/display/display_refresh.h").read_text()
    header = header.replace('#include "epdiy.h"', '').replace('#pragma once', '')
    body = '    assert(flags==DISPLAY_REFRESH_DEFAULT);\n'
    if not feedback_only:
        body += '''
    if(intent==DISPLAY_REFRESH_DETAIL)
        return update_display_area_diff_with(h,&E0470_WAVEFORM,MODE_GL16,a);
'''
    if not detail_only:
        body += '''
    if(intent==DISPLAY_REFRESH_FEEDBACK)
        return update_display_area_diff_with(h,&E0470_FOLLOW_WAVEFORM,MODE_DU,a);
'''
    if main_clock:
        body += '''
    if(intent==DISPLAY_REFRESH_MAIN_CLOCK)
        return update_display_area_diff_with(h,app_settings_main_fast_refresh()?display_main_bw_waveform():display_main_gray_waveform(),
            app_settings_main_fast_refresh()?MODE_DU:MODE_GL16,a);
'''
    elif not detail_only and not feedback_only:
        body += '''
    if(intent==DISPLAY_REFRESH_SETTLE)
        return update_display_area_full_with(h,&E0470_WAVEFORM,MODE_GL16,a);
    if(intent==DISPLAY_REFRESH_MAIN_SCROLL)
        return update_display_area_diff_padded(h,app_settings_main_refresh_mode()==APP_MAIN_REFRESH_NORMAL?
            display_main_normal_waveform():&E0470_WAVEFORM,MODE_GL16,a,1,1);
'''
    return header + '''
enum EpdDrawError display_refresh_area(EpdiyHighlevelState *h,display_refresh_intent_t intent,EpdRect a,unsigned flags) {
''' + body + '''
    assert(!"unexpected refresh intent"); return 0;
}
'''
