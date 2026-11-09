/*
 * 中文：书源解析测试不构建布局；任何意外布局访问都立即失败。
 * English: Source parsing tests never build layout; fail on every unexpected layout access.
 */
#include <stdlib.h>
#include "book_layout.h"

bool book_layout_build_blocks(const char *text, size_t len, const blk_t *blocks,
                              size_t count, EpdRect rect, int px) {
    (void)text; (void)len; (void)blocks; (void)count; (void)rect; (void)px;
    abort();
}
void book_layout_free(void) { abort(); }
void book_layout_set_runs(const html_run_t *runs, size_t count) {
    (void)runs; (void)count; abort();
}
void book_layout_set_links(const html_link_t *links, size_t count) {
    (void)links; (void)count; abort();
}
void book_layout_set_image_dims(book_layout_image_dims_fn fn, void *ctx) {
    (void)fn; (void)ctx; abort();
}
