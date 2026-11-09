"""中文：页面测试借用真实位置与书籍类型，旧断言名称只在测试中映射到会话。
English: Page tests borrow real position/book types; legacy assertion names map to the session only in tests.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def position_state(chapter=0, page=0, file_size=0):
    return f'''
#include "{ROOT / 'main/book/reader_position.h'}"
#include "{ROOT / 'main/book/reader_book.h'}"
static struct {{ reader_position_t position; reader_book_t book; }} s_reader_session = {{
    .position = {{.chapter = {chapter}, .page = {page}}},
    .book = {{.file_size = {file_size}}}
}};
#define s_chapter s_reader_session.position.chapter
#define s_page s_reader_session.position.page
#define s_path s_reader_session.book.path
#define s_file_size s_reader_session.book.file_size
#define reader_session_build_layout(session, chapter, rect, px) \\
    book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, rect, px)
'''
