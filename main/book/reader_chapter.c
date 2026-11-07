/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：保留章节读取、题头折叠、布局回滚与会话交接顺序；页面处理绘制和导航。
 * English: Preserve chapter reads, heading folding, layout rollback and session handoff order; the page owns drawing and navigation.
 */
#include "reader_chapter.h"
#include "book_source.h"
#include <stdio.h>
#include <string.h>

// 标题匹配要宽松：书内导航与正文的写法常常不同（"第一章 出发" 对 "出发"、"1. Prologue" 对
// "序章"，竖排书尤其如此）。逐字比较失败时正文标题块不会被跳过，题头与正文就各显示一次。
// Heading matching has to be forgiving: an authored navigation title rarely matches the body text
// verbatim ("Chapter 1 Departure" against "Departure", a numbered English title against its
// Japanese one), and a strict compare leaves the body heading on the page, so it shows up twice.
static bool heading_text_matches(const char *text, const char *toc, const char *heading,
                                 const char *label) {
    if (text == NULL || text[0] == 0) return false;
    const char *candidates[3] = {toc, heading, label};
    for (int i = 0; i < 3; ++i) {
        const char *cand = candidates[i];
        if (cand == NULL || cand[0] == 0) continue;
        // 互相包含即视为同一标题；短于三个字节的候选不作为依据，免得误吞正文。
        // Either containing the other counts as the same heading; candidates shorter than three
        // bytes are ignored so ordinary body text cannot be swallowed.
        if (strlen(cand) < 3) continue;
        if (strstr(text, cand) != NULL || strstr(cand, text) != NULL) return true;
    }
    return false;
}

esp_err_t reader_chapter_read(reader_chapter_t *out, size_t chapter, const char *anchor,
                             size_t source_offset, int body_height, unsigned heading_height) {
    esp_err_t err = book_chapter_load_blocks_target(chapter, anchor, source_offset,
                                                   &out->anchor_offset, &out->content);
    if (err != ESP_OK) return err;
    char *heading = out->heading, *label = out->label;
    char toc_title[128] = {0};
    if (book_has_chapter_headings() && out->content.len) {
        // 已识别的正文编号题头优先；没有时沿用书内导航或备用标题。
        // Prefer a recognized numbered body heading, then authored navigation or a fallback title.
        if (book_chapter_title(chapter, toc_title, sizeof(toc_title)) != ESP_OK)
            snprintf(toc_title, sizeof(toc_title), "第 %u 节", (unsigned)chapter + 1);
        memcpy(heading, toc_title, sizeof(out->heading));
        char *chapter_mark = strstr(heading, "章");
        if (chapter_mark && chapter_mark - heading < 24) {
            char *after = chapter_mark + strlen("章");
            while (*after == ' ' || *after == '\t' || *after == ':' || *after == '-' ||
                   !strncmp(after, "：", strlen("：")) || !strncmp(after, "、", strlen("、"))) {
                if (*after == ' ' || *after == '\t' || *after == ':' || *after == '-') ++after;
                else after += strlen("：");
            }
            if (*after && (size_t)(chapter_mark + strlen("章") - heading) < sizeof(out->label)) {
                size_t label_len = (size_t)(chapter_mark + strlen("章") - heading);
                memcpy(label, heading, label_len); label[label_len] = 0;
                memmove(heading, after, strlen(after) + 1);
            }
        }
        size_t skip_blocks = 0;
        // 最多看四个块：标题可能被拆成"第一章"、"出发"、"——副标题"这样几块。
        // Up to four blocks: a heading can arrive split across several.
        while (skip_blocks < out->content.count && skip_blocks < 4 &&
               out->content.blocks[skip_blocks].heading && out->content.blocks[skip_blocks].image < 0) {
            char body_heading[128] = {0};
            size_t len = out->content.blocks[skip_blocks].len;
            if (len >= sizeof(body_heading)) break;
            memcpy(body_heading, out->content.utf8 + out->content.blocks[skip_blocks].offset, len);
            while (len && (body_heading[len - 1] == ' ' || body_heading[len - 1] == '\t')) body_heading[--len] = 0;
            const char *trimmed = body_heading;
            while (*trimmed == ' ' || *trimmed == '\t') ++trimmed;
            if (!heading_text_matches(trimmed, toc_title, heading, label)) break;
            ++skip_blocks;
        }
        if (skip_blocks) out->lead_skip = skip_blocks < out->content.count ? out->content.blocks[skip_blocks].offset : out->content.len;
        // 首块是插图时不预留题头：那页整页留给插图，标题在它后面才排。
        // 这个条件一直是题头该不该出现的判据，横排竖排都一样。
        // An illustration at the head of the chapter reserves nothing: it owns that page and the
        // heading follows. This condition has always decided whether a sheet gets a heading at all,
        // in either setting.
        if (skip_blocks < out->content.count && out->content.blocks[skip_blocks].image >= 0) {
            out->lead_skip = 0;
            out->lead_height = 0;
        } else {
            out->lead_height = body_height > (int)heading_height
                ? heading_height : 0;
        }
    }
    return ESP_OK;
}

reader_chapter_result_t reader_chapter_apply_locked(reader_session_t *session,
                                                   reader_chapter_t *candidate,
                                                   EpdRect rect, int px,
                                                   const reader_chapter_rollback_t *rollback) {
    if (!reader_session_build_layout(session, &candidate->content, rect, px)) {
        rollback->restore_images(rollback->ctx);
        html_text_free(&candidate->content);
        rollback->restore_lead(rollback->ctx);
        const html_text_t *previous = reader_session_content(session);
        bool restored = previous->utf8 && reader_session_build_layout(session, previous, rect, px);
        return restored ? READER_CHAPTER_RESTORED : READER_CHAPTER_LOST_LAYOUT;
    }
    reader_session_adopt_chapter(session, &candidate->content);
    return READER_CHAPTER_APPLIED;
}
