/*
 * 中文：集中章节交接、布局借用与回收；书源分派和排版引擎仍保持单实例。
 * English: Centralize chapter handoff, layout borrowing and cleanup; source dispatch and layout remain singleton engines.
 */
#include "reader_session.h"
#include "reader_session_layout.h"
#include "book_source.h"
#include "book_layout.h"

static reader_session_t *s_source_owner;

esp_err_t reader_session_open(reader_session_t *session, const char *path) {
    if (!session || !path || !path[0]) return ESP_ERR_INVALID_ARG;
    if (s_source_owner) return ESP_ERR_INVALID_STATE;
    esp_err_t result = book_open(path);
    if (result == ESP_OK) s_source_owner = session;
    return result;
}

bool reader_session_has_source(const reader_session_t *session) {
    return session && s_source_owner == session;
}

const html_text_t *reader_session_content(const reader_session_t *session) {
    return &session->content;
}

bool reader_session_build_layout(reader_session_t *session, const html_text_t *chapter,
                                 EpdRect rect, int px) {
    if (!reader_session_has_source(session) || !chapter) return false;
    session->layout_owned = true;
    book_layout_set_runs(chapter->runs, chapter->run_count);
    book_layout_set_links(chapter->links, chapter->link_count);
    return book_layout_build_blocks(chapter->utf8, chapter->len, chapter->blocks,
                                    chapter->count, rect, px);
}

void reader_session_release_layout(reader_session_t *session) {
    if (!session || !session->layout_owned) return;
    book_layout_set_image_dims(NULL, NULL);
    book_layout_free();
    book_layout_set_runs(NULL, 0);
    book_layout_set_links(NULL, 0);
    session->layout_owned = false;
}

void reader_session_adopt_chapter(reader_session_t *session, html_text_t *next) {
    if (next == &session->content) return;
    html_text_free(&session->content);
    session->content = *next;
    *next = (html_text_t){0};
}

void reader_session_release_chapter(reader_session_t *session) {
    if (!session) return;
    reader_session_release_layout(session);
    html_text_free(&session->content);
}

void reader_session_close(reader_session_t *session) {
    if (!session) return;
    reader_session_release_chapter(session);
    if (s_source_owner == session) {
        book_close();
        s_source_owner = NULL;
    }
    session->book.path[0] = 0;
    session->book.title[0] = 0;
}
