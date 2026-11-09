/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：独立后端对象只读头部；首屏/锁屏的 cached 路径不扫描正文。
 * English: Independent backend objects read headers; cached first-paint/lock paths never scan body text.
 */
#include "book_metadata.h"
#include "book_epub.h"
#include "book_mobi.h"
#include <string.h>
#include <strings.h>

static bool mobi(const char *path) {
    const char *ext = path ? strrchr(path, '.') : NULL;
    return ext && !strcasecmp(ext, ".mobi");
}
bool book_metadata_supported(const char *path) {
    const char *ext = path ? strrchr(path, '.') : NULL;
    return ext && (!strcasecmp(ext, ".epub") || !strcasecmp(ext, ".mobi"));
}
esp_err_t book_metadata_read(const char *path, char *title, size_t title_cap, char *author, size_t author_cap) {
    if (mobi(path)) return book_mobi_metadata(path, title, title_cap, author, author_cap);
    return book_epub_metadata(path, title, title_cap, author, author_cap);
}
esp_err_t book_metadata_cached(const char *path, char *title, size_t title_cap, char *author, size_t author_cap) {
    if (mobi(path)) return book_mobi_metadata_cached(path, title, title_cap, author, author_cap);
    return book_epub_metadata_cached(path, title, title_cap, author, author_cap);
}
esp_err_t book_metadata_cover_bounded(const char *path, uint8_t **data, size_t *size, bool *png, size_t budget) {
    if (mobi(path)) return book_mobi_cover_bounded(path, data, size, png, budget);
    return book_epub_cover_bounded(path, data, size, png, budget);
}
