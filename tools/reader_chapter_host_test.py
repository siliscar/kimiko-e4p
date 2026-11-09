#!/usr/bin/env python3
"""中文：以真实解析、章节事务和会话验证候选隔离、回滚次序及资源回收。
English: Use real parsing, chapter transactions and sessions to verify candidate isolation, rollback order and reclamation.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UNIT = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reader_chapter.h"
#include "book_source.h"
#include "book_layout.h"
#include "book_excerpt.h"

int html_test_fail_after = -1;
static struct { void *pointer; size_t bytes; } allocations[512];
static size_t live, blocks;
static reader_session_t session;
static bool locked, prefetch_borrowed;
static unsigned builds, excerpts, closes;
static int build_failures;
static esp_err_t read_error, title_error;
static book_kind_t kind = BOOK_KIND_EPUB;
static const char *html, *title, *expected_anchor;
static size_t expected_source, expected_chapter;
static const html_run_t *layout_runs;
static const html_link_t *layout_links;
static const char *layout_text;
static const html_text_t *image_content;
static reader_chapter_t *active_candidate;
static unsigned rollback_step;
static size_t initial_live, candidate_live;
static uint8_t resolve_font(void *ctx, const char *name, size_t bytes) {
    (void)ctx; (void)name; (void)bytes; return 3;
}

void *tracked_malloc(size_t bytes) {
    void *p = malloc(bytes);
    if (!p) return NULL;
    for (size_t i = 0; i < 512; ++i) if (!allocations[i].pointer) {
        allocations[i].pointer = p; allocations[i].bytes = bytes;
        live += bytes; ++blocks; return p;
    }
    abort();
}
void tracked_free(void *p) {
    if (!p) return;
    for (size_t i = 0; i < 512; ++i) if (allocations[i].pointer == p) {
        live -= allocations[i].bytes; allocations[i].pointer = NULL; --blocks;
        free(p); return;
    }
    abort();
}
void *tracked_realloc(void *p, size_t bytes) {
    if (!p) return tracked_malloc(bytes);
    if (!bytes) { tracked_free(p); return NULL; }
    for (size_t i = 0; i < 512; ++i) if (allocations[i].pointer == p) {
        void *next = realloc(p, bytes);
        if (!next) return NULL;
        live = live - allocations[i].bytes + bytes;
        allocations[i].pointer = next; allocations[i].bytes = bytes; return next;
    }
    abort();
}
esp_err_t book_open(const char *path) { assert(path && *path); return ESP_OK; }
void book_close(void) { ++closes; }
book_kind_t book_kind(void) { return kind; }
esp_err_t book_chapter_title(size_t chapter, char *out, size_t cap) {
    assert(chapter == expected_chapter);
    if (title_error) return title_error;
    snprintf(out, cap, "%s", title); return ESP_OK;
}
esp_err_t book_chapter_load_blocks_target(size_t chapter, const char *anchor,
                                         size_t source, size_t *offset, html_text_t *out) {
    assert(!locked && chapter == expected_chapter && anchor == expected_anchor);
    assert(source == expected_source && session.position.chapter == 7 && session.position.page == 3);
    if (read_error) return read_error;
    html_font_map_t fonts = {.resolve = resolve_font};
    return html_to_blocks_with_css_target(html, strlen(html), NULL, 0, &fonts,
                                         anchor, source, offset, out);
}
void book_layout_set_runs(const html_run_t *runs, size_t count) {
    (void)count; assert(!runs || (locked && !prefetch_borrowed)); layout_runs = runs;
}
void book_layout_set_links(const html_link_t *links, size_t count) {
    (void)count; assert(!links || (locked && !prefetch_borrowed)); layout_links = links;
}
void book_layout_set_image_dims(book_layout_image_dims_fn fn, void *ctx) {
    assert(!fn && !ctx); image_content = NULL;
}
void book_layout_free(void) { layout_text = NULL; }
bool book_layout_build_blocks(const char *text, size_t len, const blk_t *table,
                              size_t count, EpdRect rect, int px) {
    (void)len; (void)table; (void)count;
    assert(locked && !prefetch_borrowed && rect.height == 900 && px == 59);
    assert(image_content && text == image_content->utf8);
    assert(layout_runs == image_content->runs && layout_links == image_content->links);
    if (rollback_step) {
        assert(rollback_step == 2 && text == session.content.utf8 && live == initial_live);
    }
    ++builds; layout_text = text;
    if (build_failures) { --build_failures; layout_text = NULL; return false; }
    return true;
}
void book_excerpt_offer(const char *path, const html_text_t *text) {
    assert(locked && !prefetch_borrowed && !strcmp(path, "test.epub"));
    assert(text == &active_candidate->content && text->utf8 == layout_text);
    assert(session.content.utf8 != text->utf8 && session.position.chapter == 7);
    ++excerpts;
}
static void restore_images(void *ctx) {
    assert(ctx == &session && locked && !rollback_step && live == candidate_live);
    assert(active_candidate->content.utf8);
    image_content = &session.content; rollback_step = 1;
}
static void restore_lead(void *ctx) {
    assert(ctx == &session && locked && rollback_step == 1 && live == initial_live);
    assert(!active_candidate->content.utf8 && !active_candidate->content.images);
    rollback_step = 2;
}
static reader_chapter_result_t apply(reader_chapter_t *candidate) {
    active_candidate = candidate; candidate_live = live; rollback_step = 0;
    // 页面先交还预绘，再应用候选布局上下文。/ The page joins prefetch before applying candidate layout context.
    locked = true; prefetch_borrowed = false; image_content = &candidate->content;
    reader_chapter_rollback_t rollback = {&session, restore_images, restore_lead};
    reader_chapter_result_t result = reader_chapter_apply_locked(&session, candidate,
                                                 (EpdRect){0, 0, 684, 900}, 59, &rollback);
    assert(session.position.chapter == 7 && session.position.page == 3);
    locked = false; return result;
}
static reader_chapter_t read_candidate(void) {
    reader_chapter_t candidate = {0}; prefetch_borrowed = true;
    assert(reader_chapter_read(&candidate, expected_chapter, expected_anchor, expected_source,
                               900, 294) == ESP_OK);
    return candidate;
}
static void heading_cases(void) {
    html = "<h1>第一章</h1><h2>出发</h2><p id='a'>正文</p>"; title = "第一章： 出发";
    reader_chapter_t c = read_candidate();
    assert(!strcmp(c.heading, "出发") && !strcmp(c.label, "第一章"));
    assert(c.lead_skip == c.content.blocks[2].offset && c.lead_height == 294);
    html_text_free(&c.content);
    html = "<h1>引言</h1><p>正文</p>";
    c = read_candidate(); assert(c.lead_skip == 0 && c.lead_height == 294); html_text_free(&c.content);
    html = "<h1>第一章</h1><img src='a.png'><p>正文</p>";
    c = read_candidate(); assert(!c.lead_skip && !c.lead_height); html_text_free(&c.content);
    html = "<h1>出发</h1><h1>出发</h1><h1>出发</h1><h1>出发</h1><h1>出发</h1>";
    c = read_candidate(); assert(c.lead_skip == c.content.blocks[4].offset); html_text_free(&c.content);
    html = "<p>正文</p>"; title_error = ESP_ERR_NOT_FOUND;
    c = read_candidate(); assert(!strcmp(c.heading, "第 10 节")); html_text_free(&c.content);
    title_error = ESP_OK; kind = BOOK_KIND_TXT;
    c = read_candidate(); assert(!*c.heading && !c.lead_height && !c.lead_skip); html_text_free(&c.content);
    kind = BOOK_KIND_EPUB; title = "第一章： 出发"; html = "";
    c = read_candidate(); assert(!c.lead_height && !*c.heading); html_text_free(&c.content);
    html = "<p>正文</p>";
    c = (reader_chapter_t){0};
    assert(reader_chapter_read(&c, expected_chapter, NULL, SIZE_MAX, 294, 294) == ESP_OK);
    assert(!c.lead_height); html_text_free(&c.content);
}
int main(void) {
    assert(reader_session_open(&session, "test.epub") == ESP_OK);
    strcpy(session.book.path, "test.epub"); session.position = (reader_position_t){7,3};
    expected_chapter = 9; expected_source = SIZE_MAX;
    heading_cases(); assert(!live && !blocks);
    html = "<h1>出发</h1><p id='a' style='font-family:test'><a href='#a'>chapter text</a></p><img src='a.png'>";
    reader_chapter_t initial = read_candidate();
    assert(initial.content.runs && initial.content.links && initial.content.anchors && initial.content.images);
    assert(apply(&initial) == READER_CHAPTER_APPLIED && !initial.content.utf8);
    initial_live = live;
    char *old_text = session.content.utf8;
    unsigned old_builds = builds, old_excerpts = excerpts;
    read_error = ESP_FAIL;
    reader_chapter_t failed = {0};
    assert(reader_chapter_read(&failed, 9, NULL, SIZE_MAX, 900, 294) == ESP_FAIL);
    assert(session.content.utf8 == old_text && live == initial_live && builds == old_builds);
    read_error = ESP_OK;
    // 逐个击中真实解析分配点，失败输出与原会话都不得泄漏。/ Fail each real parser allocation without leaking output or the old session.
    unsigned oom_cases = 0;
    for (int failure = 0; failure < 80; ++failure) {
        html_test_fail_after = failure; failed = (reader_chapter_t){0};
        esp_err_t error = reader_chapter_read(&failed, 9, NULL, SIZE_MAX, 900, 294);
        if (error == ESP_OK) { html_text_free(&failed.content); break; }
        assert(error == ESP_ERR_NO_MEM && !failed.content.utf8); ++oom_cases;
        assert(live == initial_live && session.content.utf8 == old_text);
    }
    html_test_fail_after = -1; assert(oom_cases > 5);
    expected_anchor = "a";
    reader_chapter_t candidate = read_candidate();
    assert(candidate.anchor_offset == candidate.content.blocks[1].offset);
    expected_anchor = NULL; expected_source = 35;
    reader_chapter_t source_target = read_candidate(); html_text_free(&source_target.content);
    expected_source = SIZE_MAX;
    build_failures = 1;
    assert(apply(&candidate) == READER_CHAPTER_RESTORED && rollback_step == 2);
    assert(session.content.utf8 == old_text && live == initial_live && excerpts == old_excerpts);
    assert(layout_runs == session.content.runs && layout_links == session.content.links);
    candidate = read_candidate(); build_failures = 2;
    assert(apply(&candidate) == READER_CHAPTER_LOST_LAYOUT && rollback_step == 2);
    assert(live == initial_live && session.content.utf8 == old_text);
    reader_session_close(&session); reader_session_close(&session);
    assert(!live && !blocks && closes == 1);
    // 无旧正文时不凭空重建；失败后允许重复退出。/ Without old text, skip rollback rebuild and allow repeated exit.
    assert(reader_session_open(&session, "test.epub") == ESP_OK);
    strcpy(session.book.path, "test.epub"); initial_live = 0;
    candidate = read_candidate(); build_failures = 1; old_builds = builds;
    assert(apply(&candidate) == READER_CHAPTER_LOST_LAYOUT && builds == old_builds + 1);
    reader_session_close(&session); reader_session_close(&session); assert(!live && !blocks);
    for (int cycle = 0; cycle < 100; ++cycle) {
        assert(reader_session_open(&session, "test.epub") == ESP_OK);
        strcpy(session.book.path, "test.epub");
        candidate = read_candidate(); assert(apply(&candidate) == READER_CHAPTER_APPLIED);
        initial_live = live; candidate = read_candidate();
        assert(apply(&candidate) == READER_CHAPTER_APPLIED && live == initial_live);
        reader_session_close(&session); reader_session_close(&session); assert(!live && !blocks);
    }
    printf("PASS: heading folding/image-first/TXT, anchor/source targets, %u parser OOM paths, rollback order, failed candidate isolation and 100 zero-allocation exits\n", oom_cases);
}
'''

with tempfile.TemporaryDirectory() as directory:
    folder = Path(directory)
    test, binary = folder / "chapter.c", folder / "chapter"
    test.write_text(UNIT)
    (folder / "esp_err.h").write_text((ROOT / "tools/book_source_host_stubs/esp_err.h").read_text())
    flags = ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=address,undefined", "-I" + str(folder), "-I" + str(ROOT / "main/book"),
             "-I" + str(ROOT / "tools/html_text_stubs"),
             "-I" + str(ROOT / "tools/book_source_host_stubs"),
             "-I" + str(ROOT / "tools/book_layout_stubs")]
    objects = []
    for name in ("html_text", "reader_session", "reader_chapter"):
        obj = folder / (name + ".o")
        subprocess.run(flags + ["-Dmalloc=tracked_malloc", "-Drealloc=tracked_realloc",
                                "-Dfree=tracked_free", "-c", str(ROOT / "main/book" / (name + ".c")),
                                "-o", str(obj)], check=True)
        objects.append(str(obj))
    subprocess.run(flags + [str(test), *objects, "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
