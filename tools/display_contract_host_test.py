#!/usr/bin/env python3
"""中文：验证独立页面契约、真实后端类型兼容和区域转换。
English: Verify standalone page contracts, real backend type compatibility and rectangle conversion.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix="display-contract-") as folder:
    work = Path(folder)
    (work / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n")
    # 公共契约必须在全部硬件头被禁止时独立编译，不能靠替身补出依赖。
    # The public contract must compile with every hardware header forbidden, without shims masking dependencies.
    for name in ("epdiy.h", "epd_highlevel.h", "cst836u.h", "sc7a20h_lab.h"):
        (work / name).write_text('#error "page contract imported a hardware header"\n')
    (work / "public.c").write_text('#include "app/app.h"\napp_ctx_t page;\n')
    flags = ["cc", "-Wall", "-Wextra", "-Werror", "-I" + str(work), "-Imain", "-Imain/display", "-Imain/app"]
    for language, standard in (("c", "c11"), ("c++", "c++11")):
        subprocess.run(flags + ["-x", language, "-std=" + standard, "-fsyntax-only",
                                str(work / "public.c")], cwd=ROOT, check=True)
    (work / "opaque.c").write_text('#include "app/app.h"\nvoid invalid(app_ctx_t *p) { (void)p->hl->front_fb; }\n')
    rejected = subprocess.run(flags + ["-std=c11", "-fsyntax-only", str(work / "opaque.c")],
                              cwd=ROOT, capture_output=True, text=True)
    assert rejected.returncode and "incomplete" in rejected.stderr, rejected.stderr

    # 编译仓库的实际类型定义，避免测试替身掩盖标签或字段变化。
    # Compile the repository's real type definitions so test shims cannot hide tag or field changes.
    epd = (ROOT / "components/epdiy/src/epdiy.h").read_text()
    high = (ROOT / "components/epdiy/src/epd_highlevel.h").read_text()
    rectangle = re.search(r"typedef struct \{[^{}]*\} EpdRect;", epd).group(0)
    state = re.search(r"typedef struct EpdiyHighlevelState \{[^{}]*\} EpdiyHighlevelState;", high).group(0)
    (work / "epdiy.h").write_text("#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n"
                                 "typedef struct EpdWaveform EpdWaveform;\n" + rectangle + "\n" + state)
    unit = r'''
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#ifdef BACKEND_FIRST
#include "display_geometry.h"
#include "app/app.h"
#else
#include "app/app.h"
#include "display_geometry.h"
#endif
typedef struct {
    uint8_t *front_fb, *back_fb, *difference_fb;
    bool *dirty_lines;
    uint8_t *dirty_columns;
    const EpdWaveform *waveform;
} previous_state_layout_t;
_Static_assert(sizeof(previous_state_layout_t) == sizeof(EpdiyHighlevelState), "state size");
_Static_assert(_Alignof(previous_state_layout_t) == _Alignof(EpdiyHighlevelState), "state alignment");
#define SAME_OFFSET(member) _Static_assert(offsetof(previous_state_layout_t,member) == offsetof(EpdiyHighlevelState,member), "state offset")
SAME_OFFSET(front_fb); SAME_OFFSET(back_fb); SAME_OFFSET(difference_fb);
SAME_OFFSET(dirty_lines); SAME_OFFSET(dirty_columns); SAME_OFFSET(waveform);
_Static_assert(__builtin_types_compatible_p(display_state_t, EpdiyHighlevelState), "opaque state identity");
static display_rect_t hinted;
static display_rect_t area_hint(app_ctx_t *ctx) { assert(ctx); return hinted; }
static void same(display_rect_t input) {
    hinted = input;
    app_ctx_t ctx = {0};
    app_desc_t page = {.area_hint = area_hint};
    EpdRect backend = display_rect_to_epd(page.area_hint(&ctx));
    display_rect_t output = display_rect_from_epd(backend);
    assert(input.x == output.x && input.y == output.y);
    assert(input.width == output.width && input.height == output.height);
    assert(backend.x == input.x && backend.y == input.y);
    assert(backend.width == input.width && backend.height == input.height);
}
int main(void) {
    EpdiyHighlevelState backend = {0};
    app_ctx_t page = {.hl = &backend};
    EpdiyHighlevelState *native = page.hl;
    assert(native == &backend);
    const int edge[] = {INT_MIN, -1216, -1, 0, 1, 684, 1216, INT_MAX};
    for (unsigned i=0; i<sizeof(edge)/sizeof(edge[0]); ++i)
        for (unsigned j=0; j<sizeof(edge)/sizeof(edge[0]); ++j)
            same((display_rect_t){edge[i],edge[j],edge[j],edge[i]});
    uint32_t seed=123456789;
    for (unsigned n=0; n<100000; ++n) {
        int v[4];
        for(unsigned i=0; i<4; ++i) { seed=seed*1664525u+1013904223u; v[i]=(int)(seed%100001u)-50000; }
        same((display_rect_t){v[0],v[1],v[2],v[3]});
    }
    puts("PASS: opaque state retains type/layout; callback rectangles preserve 100064 ranges without clipping");
}
'''
    (work / "test.c").write_text(unit)
    for defines in ([], ["-DBACKEND_FIRST"]):
        binary = work / "test"
        subprocess.run(flags + ["-std=c11", "-g", "-fsanitize=address,undefined"] + defines +
                       [str(work / "test.c"), "-o", str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], check=True)
    (work / "compat.cpp").write_text('#include "app/app.h"\n#include "display_geometry.h"\n'
                                    'void compatible(app_ctx_t *p, EpdiyHighlevelState *native) { p->hl=native; native=p->hl; (void)native; }\n')
    subprocess.run(flags + ["-x", "c++", "-std=c++11", "-fsyntax-only", str(work / "compat.cpp")],
                   cwd=ROOT, check=True)
    (work / "drawing.c").write_text('#include "ui/ui_menu.h"\n#include "ui/ui_quick_menu.h"\n')
    subprocess.run(flags + ["-std=c11", "-fsyntax-only", str(work / "drawing.c")], cwd=ROOT, check=True)
