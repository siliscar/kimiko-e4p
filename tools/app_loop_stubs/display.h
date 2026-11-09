/* 中文：调度测试替身。/ English: Scheduler test shim. */
#pragma once
#include "common.h"
#include "../../main/display/display_geometry.h"

void display_request_full_refresh(void);

enum EpdDrawError update_display_click_feedback(EpdiyHighlevelState* hl,EpdRect area);


enum EpdDrawError update_display_white_restore(EpdiyHighlevelState *hl, EpdRect area);
