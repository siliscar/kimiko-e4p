"""中文：页面回归复用真实重排事务与 UI 绑定。/ English: Page regressions reuse real reflow transactions and UI bindings."""
from pathlib import Path
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]


def reflow_code(kind, *extra):
    code = f'\n#include "{ROOT / "main/book/reader_reflow.h"}"\n'
    for name in (kind, *extra):
        if name == "option_locked":
            code += function("reader_reflow_set_option", ROOT / "main/book/reader_reflow.c") + "\n"
        code += function("reader_reflow_" + name, ROOT / "main/book/reader_reflow.c") + "\n"
    for name in ("reader_reflow_begin", "reader_reflow_build", "reader_reflow_context"):
        code += function(name, ROOT / "main/apps/app_book.c") + "\n"
    return code
