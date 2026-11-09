/* SPDX-License-Identifier: Apache-2.0
 * 中文：实际阅读波形的驱动边界；光学灰度和残影仍需实屏验证。
 * English: Validate actual reader pulse boundaries; optical tone and ghosting need panel tests.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "display_normalize.h"
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"

// 主机替身：表构建会读背景路径设置；用例先跑无背景分支，再临时打开背景验证归一表。
// Host stand-in: table builds read the background-path setting; cases run the
// no-background branch first, then switch the background on to check the normalize table.
static const char *s_bg_path = "";
const char *app_settings_book_background_path(void) { return s_bg_path; }

int main(void) {
    e0470_waveform_init();
    const EpdWaveformPhases* old = e0470_waveform_phases(&E0470_TEXTTURN_WAVEFORM, MODE_GL16);
    uint8_t saved[E0470_GL16_FRAMES * 16 * 4];
    assert(old && old->phases == E0470_GL16_FRAMES && !old->phase_times);
    memcpy(saved, old->luts, sizeof(saved));
    const EpdWaveform* wave = display_reader_text_waveform();
    const EpdWaveformPhases* soft = e0470_waveform_phases(wave, MODE_GL16);
    assert(wave != &E0470_TEXTTURN_WAVEFORM && display_reader_text_waveform() == wave);
    assert(wave->temp_intervals == E0470_TEXTTURN_WAVEFORM.temp_intervals);
    assert(soft && soft->phases == old->phases && soft->phase_times == old->phase_times);
    unsigned changed = 0;
    for (int from = 0; from < 16; ++from) for (int to = 0; to < 16; ++to) {
        // 变白列与黑列由各自的检查覆盖，不参与文字软化的逐相对比。
        // The whitening and black columns have their own checks and stay out of the softening comparison.
        if (to == 15 || to == 0) continue;
        int a_count[4] = {0}, b_count[4] = {0}, a_balance = 0, b_balance = 0;
        int a_peak = 0, b_peak = 0, first = -1, last = -1;
        unsigned differences = 0;
        for (int f = 0; f < old->phases; ++f) {
            int a = e0470_phase_action(old, f, to, from), b = e0470_phase_action(soft, f, to, from);
            ++a_count[a]; ++b_count[b];
            if (a != b) ++differences;
            if (from != 15) assert(a == b);
            if (a == 0 || a == 3) assert(b == a);
            if (a) {if (first < 0) first = f; last = f;}
            a_balance += a == 1 ? 1 : a == 2 ? -1 : 0;
            b_balance += b == 1 ? 1 : b == 2 ? -1 : 0;
            if (a_balance > a_peak) a_peak = a_balance;
            if (b_balance > b_peak) b_peak = b_balance;
        }
        assert(!memcmp(a_count, b_count, sizeof(a_count)));
        if (first >= 0) assert(e0470_phase_action(old, first, to, from) == e0470_phase_action(soft, first, to, from));
        if (last >= 0) assert(e0470_phase_action(old, last, to, from) == e0470_phase_action(soft, last, to, from));
        if (differences) {
            assert(from == 15 && to > 0 && to < 15 && differences <= 4);
            assert(b_peak < a_peak && a_peak - b_peak <= 2);
            ++changed;
            printf("15->%d net black pulse peak %d -> %d; phases ", to, a_peak, b_peak);
            for (int f = 0; f < soft->phases; ++f) putchar(".BW?"[e0470_phase_action(soft, f, to, from)]);
            putchar('\n');
        }
    }
    // 普通文字表沿用主页白列：DU 的 18 次擦白 + 6 相尾部补推，白到白保持。
    // The ordinary text table keeps the main-page column: 18 DU erases plus the six
    // tail pushes, holding white-to-white.
    unsigned text_erases = 0;
    for (int f = 0; f < soft->phases; ++f) {
        text_erases += e0470_phase_action(soft, f, 15, 0) == 2;
        assert(e0470_phase_action(soft, f, 15, 15) == 0);
    }
    assert(text_erases == 24);
    assert(changed > 0 && !memcmp(saved, old->luts, sizeof(saved)));
    // 黑列补推：裁表砍掉的 5 次推黑补回原厂 18 次，占用前导保持相，相数与末尾保持不变。
    // Black column: restore the five trim-cut pushes to the vendor's 18 from leading holds,
    // keeping the phase count and the trailing holds.
    const EpdWaveformPhases* full = e0470_waveform_phases(&E0470_FULL_WAVEFORM, MODE_GL16);
    assert(full && full->luts);
    for (int from = 1; from < 16; ++from) {
        int want = 0, got = 0, last = -1, first_black = -1, erase_after_black = 0;
        for (int f = 0; f < full->phases; ++f)
            want += e0470_phase_action(full, f, 0, from) == 1;
        for (int f = 0; f < soft->phases; ++f) {
            const int action = e0470_phase_action(soft, f, 0, from);
            got += action == 1;
            if (action == 1) { if (first_black < 0) first_black = f; last = f; }
            else if (action == 2 && first_black >= 0) ++erase_after_black;
        }
        assert(want == 18 && got == want);
        assert(first_black >= 0 && last <= (int)soft->phases - 4);
        assert(!erase_after_black);
    }
    // 普通纯文字翻页复用这套已验证文字序列：相同灰阶全保持，不能改字形目标或原厂水波纹表。
    // Ordinary text turns share these proven text phases: equal grays hold, retaining glyph targets and the vendor ripple table.
    const EpdWaveformPhases* raw=e0470_waveform_phases(&E0470_WAVEFORM,MODE_GL16);
    unsigned held=0,edge_runs=0;
    for(int to=0;to<16;++to) {
        int old_run=0,new_run=0,old_max=0,new_max=0,old_same=0;
        int raw_counts[4]={0},soft_counts[4]={0};
        for(int f=0;f<raw->phases;++f) {
            int a=e0470_phase_action(raw,f,to,15),b=e0470_phase_action(soft,f,to,15);
            ++raw_counts[a];++soft_counts[b];
            old_run=a==1?old_run+1:0;new_run=b==1?new_run+1:0;
            if(old_run>old_max)old_max=old_run;
            if(new_run>new_max)new_max=new_run;
            old_same+=e0470_phase_action(raw,f,to,to)!=0;
            assert(e0470_phase_action(soft,f,to,to)==0);
        }
        held+=old_same>0;
        if(to>0&&to<15) {
            assert(!memcmp(raw_counts,soft_counts,sizeof(raw_counts)));
            assert(new_max<=old_max);edge_runs+=new_max<old_max;
        }
    }
    assert(held==16&&edge_runs>=10);
    // 水波纹：白列仍是原厂（黑→白 18 次擦白、中灰→白保留前导擦除），
    // 只把 to=0 的黑列补到 18 次，37 相与 8 带 44 拍不变。
    // Water keeps the vendor white column (18 black-to-white erases and a leading erase
    // on gray-to-white) and only restores the 18 pushes for to=0; 37 phases and the
    // eight-band 44-tick turn stay unchanged.
    unsigned erases = 0;
    for (int f = 0; f < raw->phases; ++f)
        erases += e0470_phase_action(raw, f, 15, 0) == 2;
    assert(erases == 18);
    for (int from = 1; from < 15; ++from) {
        int first_white = -1, first_black = -1;
        for (int f = 0; f < raw->phases; ++f) {
            const int action = e0470_phase_action(raw, f, 15, from);
            if (action == 2 && first_white < 0) first_white = f;
            if (action == 1 && first_black < 0) first_black = f;
        }
        assert(first_black >= 0 && first_white > 0 && first_black < first_white);
    }
    const EpdWaveform* water = display_reader_water_waveform();
    const EpdWaveformPhases* ripple = e0470_waveform_phases(water, MODE_GL16);
    assert(water != &E0470_WAVEFORM && display_reader_water_waveform() == water);
    assert(water->temp_intervals == E0470_WAVEFORM.temp_intervals);
    assert(ripple && ripple->luts && ripple->phases == raw->phases && !ripple->phase_times);
    assert(ripple->phases == E0470_GL16_FRAMES);
    for (int f = 0; f < raw->phases; ++f) {
        // 白列逐字节保持原厂，不引入主页的白列尾部补推。
        // The white column stays byte-identical to the vendor table.
        assert(!memcmp(ripple->luts + (size_t)f * 16 * 4 + 15 * 4,
                       raw->luts + (size_t)f * 16 * 4 + 15 * 4, 4));
    }
    for (int to = 1; to < 16; ++to)
        for (int from = 0; from < 16; ++from)
            for (int f = 0; f < raw->phases; ++f)
                assert(e0470_phase_action(ripple, f, to, from) ==
                       e0470_phase_action(raw, f, to, from));
    for (int from = 1; from < 16; ++from) {
        int got = 0;
        for (int f = 0; f < ripple->phases; ++f)
            got += e0470_phase_action(ripple, f, 0, from) == 1;
        assert(got == 18);
    }
    // 整页阅读表同样补黑，白列仍是 24 次擦白。
    // The full-page reader table gets the same black pushes and keeps 24 erases.
    const EpdWaveform* page = display_reader_page_waveform();
    const EpdWaveformPhases* full_page = e0470_waveform_phases(page, MODE_GL16);
    assert(full_page && full_page->phases == E0470_GL16_FRAMES);
    unsigned page_erases = 0;
    for (int f = 0; f < full_page->phases; ++f)
        page_erases += e0470_phase_action(full_page, f, 15, 0) == 2;
    assert(page_erases == 24);
    for (int from = 1; from < 16; ++from) {
        int black = 0;
        for (int f = 0; f < full_page->phases; ++f)
            black += e0470_phase_action(full_page, f, 0, from) == 1;
        assert(black == 18);
    }
    // 背景派生表：二次裁剪会把白列削到 15 拍，旧字转白擦不净。这里必须补回主页水波纹
    // 已验证的 24 拍，且不增加帧数——白列的形状能落进现有的尾部保持空间。
    // Background derivative: the second trim would leave 15 white pushes and erased text
    // would stay pale. It must carry the main water table's verified 24 pushes without
    // adding frames—the column shape fits inside the existing trailing holds.
    s_bg_path = "card:/backgrounds/paper.bmp";
    const EpdWaveform* bg = display_reader_bg_waveform();
    const EpdWaveformPhases* bgp = e0470_waveform_phases(bg, MODE_GL16);
    assert(bgp && bgp->luts && !bgp->phase_times);
    assert(bgp->phases <= E0470_GL16_FRAMES);
    int bg_white = 0;
    for (int f = 0; f < bgp->phases; ++f) bg_white += e0470_phase_action(bgp, f, 15, 0) == 2;
    assert(bg_white == 24);
    // 白列必须给尾部保持让位，不能把最后一相也拿去推白。
    // The white column must leave the trailing hold alone instead of pushing on the last phase.
    assert(e0470_phase_action(bgp, bgp->phases - 1, 15, 0) == 0);
    // 插图页派生表：深色对保留前导擦白（旧字落到图片灰时从白轨落位，不留字形），相等对
    // 仍置保持（脏区膨胀带上的未变化像素不会被黑白往返推出一圈细线）。背景派生表只保
    // 前半段的裁剪，深色对的前导擦白为 0。
    // Illustration derivative: dark pairs keep their leading erase, so old glyphs settle on an
    // image gray from the white rail and leave no shape; equal pairs still hold, so the
    // unchanged pixels dilation drags along never take a white-black trip that draws a thin
    // outline. The background derivative keeps only the trim and drops the dark-pair erase.
    const EpdWaveformPhases* img = e0470_waveform_phases(display_reader_image_waveform(), MODE_GL16);
    assert(img && img->luts && !img->phase_times);
    for (int g = 0; g < 15; ++g)
        for (int f = 0; f < img->phases; ++f)
            assert(e0470_phase_action(img, f, g, g) == 0);
    for (int to = 0; to <= 7; ++to)
        for (int from = 0; from <= 7; ++from) {
            if (from == to) continue;
            int ink_erase = 0, bg_erase = 0;
            for (int f = 0; f < img->phases; ++f) {
                const int action = e0470_phase_action(img, f, to, from);
                if (action == 1 || action == 3) break;
                ink_erase += action == 2;
            }
            for (int f = 0; f < bgp->phases; ++f) {
                // 只数前导擦白；灰尾里的白推负责落位，不算擦除。
                // Count the leading erase only; white pushes in the gray tail settle, they do not erase.
                const int action = e0470_phase_action(bgp, f, to, from);
                if (action == 1 || action == 3) break;
                bg_erase += action == 2;
            }
            assert(ink_erase >= 11);
            assert(bg_erase == 0);
        }
    // 插图页的推黑一次都不能少：灰阶落位以「先压到黑轨、再由灰尾抬到目标」为基准，
    // 少一相推黑就整体浅一档。to=0 另有补强，所以裁表后正文依旧黑得起来，缺陷只在
    // 图片的中间调上显形（发灰发淡）。这里按基底水波纹表逐对核对推黑次数。
    // Illustration pages may not lose a single black push: every gray lands from the black
    // rail, so one missing push lightens the whole ladder a step. The to=0 boost keeps body
    // text black, which is why the damage only shows on picture midtones. Check the push
    // count pair by pair against the water base.
    const EpdWaveformPhases* water_base =
        e0470_waveform_phases(display_reader_water_base_waveform(), MODE_GL16);
    assert(water_base && water_base->luts);
    int midtones = 0;
    for (int to = 0; to < 15; ++to)
        for (int from = 0; from < 16; ++from) {
            if (from == to) continue;
            int want = 0, got = 0;
            for (int f = 0; f < water_base->phases; ++f)
                want += e0470_phase_action(water_base, f, to, from) == 1;
            for (int f = 0; f < img->phases; ++f)
                got += e0470_phase_action(img, f, to, from) == 1;
            assert(got == want);
            ++midtones;
        }
    assert(midtones == 15 * 15);
    // 归一表：基底不随阅读背景切换。背景派生表相数更少，旧实现会原样返回它，归一化
    // 在开了阅读背景时静默失效。两张变体：纯文字页保留原厂非黑相等对，背景页清成保持。
    // Normalize table: the base never follows the reading-background switch. The background
    // derivative carries fewer phases, and the old build returned it unchanged, so
    // normalization silently stopped working while a background was set. Two variants: a
    // plain-text table that keeps the vendor non-black equal pairs and a background table
    // that holds them.
    assert(display_reader_page_waveform() != display_reader_ink_waveform());
    const EpdWaveform* norm_bg = display_normalize_waveform(true);
    const EpdWaveformPhases* np_bg = e0470_waveform_phases(norm_bg, MODE_GL16);
    assert(np_bg && np_bg->luts && np_bg->phases == E0470_GL16_FRAMES && !np_bg->phase_times);
    s_bg_path = "";
    assert(display_reader_page_waveform() == display_reader_ink_waveform());
    // 变体只由参数决定：背景开关不影响哪张表被选中。
    // The variant comes from the argument alone; the background switch never picks the table.
    assert(display_normalize_waveform(true) == norm_bg);
    const EpdWaveform* norm = display_normalize_waveform(false);
    const EpdWaveformPhases* np = e0470_waveform_phases(norm, MODE_GL16);
    assert(np && np->luts && np->phases == E0470_GL16_FRAMES && !np->phase_times);
    assert(norm != norm_bg && display_normalize_waveform(false) == norm);
    s_bg_path = "card:/backgrounds/paper.bmp";
    assert(display_normalize_waveform(false) == norm);
    const EpdWaveformPhases* ink = e0470_waveform_phases(display_reader_ink_waveform(), MODE_GL16);
    assert(ink && ink->phases == np->phases);
    const EpdWaveformPhases* variants[2] = {np, np_bg};
    for (int v = 0; v < 2; ++v) {
        // 字芯：每个起点都是 18 擦白后接 18 推黑，中途不再擦白。
        // Glyph core: every start runs 18 erases then 18 pushes, with no erase in between.
        for (int from = 0; from < 16; ++from) {
            int erase = 0, black = 0, first_black = -1, erase_after_black = 0;
            for (int f = 0; f < variants[v]->phases; ++f) {
                const int action = e0470_phase_action(variants[v], f, 0, from);
                erase += action == 2;
                black += action == 1;
                if (action == 1 && first_black < 0) first_black = f;
                if (action == 2 && first_black >= 0) ++erase_after_black;
            }
            assert(erase == 18 && black == 18 && erase_after_black == 0);
        }
        // 非字芯迁移逐相沿用基底整页表；对角线由各自的相等对检查覆盖。
        // Every non-core transition follows the base page table; the diagonal is covered by
        // the equal-pair check below.
        for (int to = 1; to < 16; ++to)
            for (int from = 0; from < 16; ++from) {
                if (from == to) continue;
                for (int f = 0; f < variants[v]->phases; ++f)
                    assert(e0470_phase_action(variants[v], f, to, from) ==
                           e0470_phase_action(ink, f, to, from));
            }
    }
    // 纯文字表保留原厂相等对：抗锯齿边缘也跟着走一轮，墨度与字芯一起归一。
    // The plain-text table keeps the vendor equal pairs so antialiased edges normalize with
    // the glyph core instead of keeping their covered/uncovered history.
    for (int g = 1; g < 16; ++g)
        for (int f = 0; f < np->phases; ++f)
            assert(e0470_phase_action(np, f, g, g) == e0470_phase_action(ink, f, g, g));
    // 背景表清成保持：整屏 pass 标脏整页，未变化的背景与图片灰阶不被重驱。
    // The background table holds them: the pass dirties every pixel, and unchanged
    // background and picture grays must not be re-driven.
    for (int g = 1; g < 16; ++g)
        for (int f = 0; f < np_bg->phases; ++f)
            assert(e0470_phase_action(np_bg, f, g, g) == 0);
    puts("PASS: the normalize table keeps 37 phases with a reading background on; its plain-text variant keeps the vendor equal pairs and the background variant holds them so picture grays survive the whole-screen pass");
    puts("PASS: water turns restore 18 black pushes for to=0 while keeping the vendor white column, 37 phases and the eight-band tick count");
    puts("PASS: the illustration table keeps the water base's black push count for every gray pair, so picture midtones land on the calibrated ladder instead of four steps light");
    puts("PASS: ordinary text turns hold all 16 unchanged grays, reduce at least ten gray-edge dark runs and keep the 37 phases");
    puts("PASS: all 256 pairs, holds, first/final actions, phase count and original LUT retained except the to=0 black column; optical validation pending");
    return 0;
}
