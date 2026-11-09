#!/usr/bin/env python3
"""中文：用越界依赖和缺失入口验证结构检查确实能阻止回归。
English: Verify that forbidden dependencies and missing entries actually fail structural checks.
"""
from pathlib import Path
import tempfile
from check_architecture import ROOT, check


def main():
    errors = check(ROOT)
    assert not errors, "\n".join(errors)
    with tempfile.TemporaryDirectory(prefix="architecture-") as folder:
        root = Path(folder)
        files = {
            "main/CMakeLists.txt": 'idf_component_register(SRCS "app/loop.c" "image/image.c" "font/font.c" "book/book_shelf_category_state.c" "book/book_shelf_covers.c" "ui/ui_reader_settings.c" PRIV_INCLUDE_DIRS "." "app" "apps" "book" "ui" "image" "font" "services" "settings")\n',
            "main/app/loop.c": '#include "contracts.h"\n',
            "main/app/contracts.h": "#pragma once\n",
            "main/apps/page.h": "#pragma once\n",
            "main/book/book.h": "#pragma once\n",
            "main/book/book_shelf_category_state.c": "void categories(void) {}\n",
            "main/book/book_shelf_covers.c": "void covers(void) {}\n",
            "main/ui/ui_reader_settings.c": "void settings_view(void) {}\n",
            "main/settings/settings.h": "#pragma once\n",
            "main/image/image.c": "void image(void) {}\n",
            "main/font/font.c": "void font(void) {}\n",
            "main/services/policy.h": "#pragma once\n",
            "components/driver/driver.c": "void driver(void) {}\n",
            "tools/run_reader_baseline.py": 'SUITES = {"example": ["python3", "tools/entry.py"]}\n',
            "tools/entry.py": "pass\n",
        }
        for name, source in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source)
        assert not check(root)
        cases = [
            ("main/app/loop.c", 'void f(void) { update_display_area_diff_with(); }\n', "waveform selection must stay in display"),
            ("main/app/loop.c", 'const void *w = &E0470_FULL_WAVEFORM;\n', "waveform selection must stay in display"),
            ("main/app/loop.c", '#include "display_waveform_catalog.h"\n', "waveform selection must stay in display"),
            ("main/ui/ui_reader_settings.c", '#include "settings.h"\n', "reader settings view must not"),
            ("main/ui/ui_reader_settings.c", '#include "book.h"\n', "reader settings view must not"),
            ("main/ui/ui_reader_settings.c", '#include "../apps/page.h"\n', "reader settings view must not"),
            ("main/app/loop.c", '#include "../apps/page.h"\n', "app coordinators"),
            ("main/image/image.c", '#include <book.h>\n', "generic image"),
            ("main/font/font.c", '#include "policy.h"\n', "font engines"),
            ("components/driver/driver.c", '#include "contracts.h"\n', "components must not"),
            ("main/book/book_shelf_category_state.c", '#include "settings.h"\n', "shelf category state must not"),
            ("main/app/loop.c", 'book_shelf_categories_t state;\nvoid reset(void) { state.selected = 0; }\n', "through its API"),
            ("main/app/loop.c", 'book_shelf_covers_t state;\nvoid reset(void) { state.pending = 0; }\n', "shelf cover state must be accessed"),
            ("main/book/book_shelf_covers.c", '#include "settings.h"\n', "shelf cover state must not depend"),
            ("main/CMakeLists.txt", files["main/CMakeLists.txt"].replace("PRIV_INCLUDE_DIRS", "INCLUDE_DIRS"), "PRIV_INCLUDE_DIRS"),
            ("main/CMakeLists.txt", files["main/CMakeLists.txt"].replace('"font/font.c"', '"missing.c"'), "source is absent"),
            ("tools/run_reader_baseline.py", 'SUITES = {"missing": ["python3", "tools/missing.py"]}\n', "missing tools/missing.py"),
        ]
        for name, source, expected in cases:
            path = root / name
            path.write_text(source)
            assert any(expected in error for error in check(root)), (name, expected)
            path.write_text(files[name])
            assert not check(root)
        # 根目录和真实编译头路径也属于结构契约。/ Root ownership and actual compiler include paths are structural contracts.
        for name in ("main/orphan.h", "main/orphan.c"):
            path = root / name
            path.write_text("// 临时功能文件。/ Temporary feature file.\n")
            assert any("feature files must live" in error for error in check(root))
            path.unlink()
            assert not check(root)
        cmake = root / "main/CMakeLists.txt"
        cmake.write_text(files["main/CMakeLists.txt"].replace(' "settings"', ""))
        page = root / "main/app/loop.c"
        page.write_text('#include "settings.h"\n')
        assert any("outside declared private include paths" in error for error in check(root))
        cmake.write_text(files["main/CMakeLists.txt"])
        assert not check(root)
        page.write_text('#include "../missing.h"\n')
        assert any("missing relative include" in error for error in check(root))
        page.write_text(files["main/app/loop.c"])
        assert not check(root)
        # 注释示例不会变成假依赖。/ Commented examples must not become false dependencies.
        (root / "main/app/loop.c").write_text('// #include "../apps/page.h"\n/*\n#include "../apps/page.h"\n*/\n')
        assert not check(root)
    print("architecture: PASS (real tree and twenty-one rejecting fixtures)")


if __name__ == "__main__":
    main()
