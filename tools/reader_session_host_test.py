#!/usr/bin/env python3
"""中文：运行真实会话与 HTML 释放代码，核对书源排他、章节交接和回收。
English: Exercise the actual session and HTML cleanup for exclusive sources, chapter handoff and reclamation.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UNIT = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reader_session.h"
#include "book_layout.h"

bool book_layout_build_blocks(const char *text, size_t len, const blk_t *table,
                              size_t count, EpdRect rect, int px) {
    (void)text; (void)len; (void)table; (void)count; (void)rect; (void)px;
    abort();
}
void book_layout_free(void) { abort(); }
void book_layout_set_runs(const html_run_t *runs, size_t count) { (void)runs; (void)count; abort(); }
void book_layout_set_links(const html_link_t *links, size_t count) { (void)links; (void)count; abort(); }
void book_layout_set_image_dims(book_layout_image_dims_fn fn, void *ctx) { (void)fn; (void)ctx; abort(); }

int html_test_fail_after = -1;
static struct {void *pointer; size_t bytes;} allocations[512];
static size_t live, blocks;
static unsigned opens, closes;
static esp_err_t open_result = ESP_OK;

void *tracked_malloc(size_t bytes) {
    void *pointer = malloc(bytes);
    if (!pointer) return NULL;
    for (size_t i = 0; i < 512; ++i) if (!allocations[i].pointer) {
        allocations[i].pointer = pointer; allocations[i].bytes = bytes;
        live += bytes; ++blocks; return pointer;
    }
    abort();
}
void tracked_free(void *pointer) {
    if (!pointer) return;
    for (size_t i = 0; i < 512; ++i) if (allocations[i].pointer == pointer) {
        live -= allocations[i].bytes; allocations[i].pointer = NULL; --blocks;
        free(pointer); return;
    }
    abort();
}
void *tracked_realloc(void *pointer, size_t bytes) {
    if (!pointer) return tracked_malloc(bytes);
    if (!bytes) {tracked_free(pointer); return NULL;}
    for (size_t i = 0; i < 512; ++i) if (allocations[i].pointer == pointer) {
        void *next = realloc(pointer, bytes);
        if (!next) return NULL;
        live = live - allocations[i].bytes + bytes;
        allocations[i].pointer = next; allocations[i].bytes = bytes; return next;
    }
    abort();
}
esp_err_t book_open(const char *path) {assert(path && path[0]); ++opens; return open_result;}
void book_close(void) {++closes;}
static uint8_t resolve_font(void *ctx, const char *name, size_t bytes) {
    (void)ctx; (void)name; (void)bytes; return 3;
}
static html_text_t chapter(void) {
    const char *html = "<p id='a' style='font-family:test'><a href='#a'>chapter text</a></p><img src='one.png'>";
    html_font_map_t fonts = {.resolve=resolve_font};
    html_text_t next = {0}; size_t offset = 0;
    assert(html_to_blocks_with_css_target(html, strlen(html), NULL, 0, &fonts,
                                         "a", SIZE_MAX, &offset, &next) == ESP_OK);
    assert(next.utf8 && next.blocks && next.images && next.image_count == 1);
    assert(next.runs && next.run_count && next.links && next.anchors && next.strings);
    return next;
}
int main(void) {
    reader_session_t first = {0}, second = {0};
    first.position = (reader_position_t){.chapter=7,.page=3};
    second.position = (reader_position_t){.chapter=2,.page=1};
    assert(!reader_session_has_source(NULL) && !reader_session_has_source(&first));
    assert(reader_session_open(NULL, "book.epub") == ESP_ERR_INVALID_ARG);
    assert(reader_session_open(&first, NULL) == ESP_ERR_INVALID_ARG);
    assert(reader_session_open(&first, "") == ESP_ERR_INVALID_ARG && opens == 0);
    open_result = ESP_ERR_NO_MEM;
    assert(reader_session_open(&first, "book.epub") == ESP_ERR_NO_MEM);
    assert(!reader_session_has_source(&first));
    open_result = ESP_OK;
    assert(reader_session_open(&first, "book.epub") == ESP_OK);
    assert(reader_session_has_source(&first));
    strcpy(first.book.path, "book.epub");
    strcpy(first.book.title, "First book");
    strcpy(second.book.path, "stale.txt");
    strcpy(second.book.title, "Stale book");
    unsigned opened = opens;
    assert(reader_session_open(&second, "other.txt") == ESP_ERR_INVALID_STATE && opens == opened);
    assert(reader_session_open(&first, "other.txt") == ESP_ERR_INVALID_STATE && opens == opened);
    reader_session_close(&second); reader_session_close(NULL); assert(closes == 0);
    assert(!second.book.path[0] && !second.book.title[0]);
    assert(!strcmp(first.book.path, "book.epub") && !strcmp(first.book.title, "First book"));
    for (int i = 0; i < 100; ++i) {
        html_text_t next = chapter(); char *text = next.utf8;
        reader_session_adopt_chapter(&first, &next);
        assert(next.utf8 == NULL && next.images == NULL && !next.image_count);
        assert(reader_session_content(&first)->utf8 == text && live > 0);
        size_t held = live, held_blocks = blocks;
        html_text_t failed = {0}; html_test_fail_after = 0;
        assert(html_to_blocks("<p>failure</p>", 14, &failed) == ESP_ERR_NO_MEM);
        html_text_free(&failed); html_test_fail_after = -1;
        assert(reader_session_content(&first)->utf8 == text && live == held && blocks == held_blocks);
        assert(first.position.chapter == 7 && first.position.page == 3);
        assert(second.position.chapter == 2 && second.position.page == 1);
        html_text_t candidate = chapter();
        html_text_free(&candidate);
        assert(reader_session_content(&first)->utf8 == text && live == held && blocks == held_blocks);
        reader_session_release_chapter(&first); reader_session_release_chapter(&first);
        assert(live == 0 && blocks == 0 && reader_session_has_source(&first));
    }
    html_text_t one = chapter(); reader_session_adopt_chapter(&first, &one);
    size_t one_size = live;
    html_text_t two = chapter(); reader_session_adopt_chapter(&first, &two);
    assert(live == one_size);
    reader_session_close(&first); reader_session_close(&first);
    assert(!live && !blocks && closes == 1 && !reader_session_has_source(&first));
    assert(!first.book.path[0] && !first.book.title[0]);
    assert(reader_session_open(&second, "other.txt") == ESP_OK);
    reader_session_close(&first); assert(reader_session_has_source(&second) && closes == 1);
    reader_session_close(&second); assert(closes == 2 && !live);
    puts("PASS: exclusive source ownership, failed open preservation, full parsed chapter transfer, candidate rejection, repeated close and zero live allocations across 100 cycles");
}
'''

with tempfile.TemporaryDirectory() as directory:
    folder = Path(directory)
    test, binary = folder / "session.c", folder / "session"
    test.write_text(UNIT)
    (folder / "esp_err.h").write_text((ROOT / "tools/book_source_host_stubs/esp_err.h").read_text())
    flags = ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=address,undefined", "-I" + str(ROOT / "main/book"),
             "-I" + str(folder),
             "-I" + str(ROOT / "tools/html_text_stubs"),
             "-I" + str(ROOT / "tools/book_layout_stubs")]
    objects = []
    for name in ("html_text", "reader_session"):
        obj = folder / (name + ".o")
        subprocess.run(flags + ["-Dmalloc=tracked_malloc", "-Drealloc=tracked_realloc",
                                "-Dfree=tracked_free", "-c", str(ROOT / "main/book" / (name + ".c")),
                                "-o", str(obj)], check=True)
        objects.append(str(obj))
    subprocess.run(flags + [str(test), *objects, "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
