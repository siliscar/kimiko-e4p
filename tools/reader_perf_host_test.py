#!/usr/bin/env python3
"""中文：真实测量模块的代次、退出和内存监控回归。
English: Exercise the real profiler's generations, completion and heap monitoring.
"""
from pathlib import Path
import json
import subprocess
import tempfile
import summarize_reader_perf as summary

ROOT = Path(__file__).resolve().parents[1]
STUBS = {
    "sdkconfig.h": "#define CONFIG_READER_PERF 1\n#define CONFIG_READER_MEMORY 1\n",
    "esp_timer.h": "#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
    "esp_log.h": '#include <stdio.h>\n#define ESP_LOGI(tag, ...) ((void)(tag), printf(__VA_ARGS__), puts(""))\n',
    "esp_heap_caps.h": """
#include <stddef.h>
#define ESP_OK 0
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_SPIRAM 2
#define MALLOC_CAP_8BIT 4
int heap_caps_monitor_local_minimum_free_size_start(void);
int heap_caps_monitor_local_minimum_free_size_stop(void);
size_t heap_caps_get_allocated_size(void *pointer);
size_t heap_caps_get_free_size(unsigned caps);
size_t heap_caps_get_minimum_free_size(unsigned caps);
size_t heap_caps_get_largest_free_block(unsigned caps);
""",
    "freertos/FreeRTOS.h": """
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
""",
    "freertos/task.h": "unsigned uxTaskGetStackHighWaterMark(void *task);\n",
    "ttf_font.h": """
#include <stdint.h>
typedef struct { uint32_t glyphs,hits,misses; int64_t read_us,raster_us,total_us; } ttf_bench_stats_t;
void ttf_bench_begin(void);
void ttf_bench_end(ttf_bench_stats_t *out);
""",
}
CODE = r'''
#include <assert.h>
#include "reader_perf.c"
#if CONFIG_READER_MEMORY
#define EXPECT_MEMORY_READY(value) assert(reader_perf_memory_ready()==(value))
#else
#define EXPECT_MEMORY_READY(value) ((void)0)
#endif
static int64_t now;
static int starts,stops,benches,ends;
static bool monitor_owned;
int64_t esp_timer_get_time(void){return now;}
int heap_caps_monitor_local_minimum_free_size_start(void){++starts;if(monitor_owned)return -1;monitor_owned=true;return ESP_OK;}
int heap_caps_monitor_local_minimum_free_size_stop(void){assert(monitor_owned);++stops;monitor_owned=false;return ESP_OK;}
size_t heap_caps_get_free_size(unsigned c){return c&1?1000:2000;}
size_t heap_caps_get_minimum_free_size(unsigned c){assert(monitor_owned);return c&1?800:1800;}
size_t heap_caps_get_largest_free_block(unsigned c){return c&1?600:1600;}
unsigned uxTaskGetStackHighWaterMark(void *task){(void)task;return 512;}
void ttf_bench_begin(void){++benches;}
void ttf_bench_end(ttf_bench_stats_t *out){++ends;*out=(ttf_bench_stats_t){.read_us=7,.raster_us=8,.hits=9,.misses=10};}
static void early_return(void){READER_PERF_SCOPE(READER_PERF_PARSE);now+=5;return;}
int main(void){
 reader_perf_finish(false);assert(!starts&&!stops);EXPECT_MEMORY_READY(true);
 now=10;reader_perf_input_begin();now=20;reader_perf_begin("turn");EXPECT_MEMORY_READY(false);
 reader_perf_scope_t old=reader_perf_scope_begin(READER_PERF_SOURCE);
 {READER_PERF_SCOPE(READER_PERF_ACTION);now+=20;early_return();}
 now=50;reader_perf_scope_t display=reader_perf_scope_begin(READER_PERF_DISPLAY);
 now=80;reader_perf_scope_end(&display);reader_perf_scope_end(&old);
 reader_perf_scope_t invalid=reader_perf_scope_begin((reader_perf_stage_t)-1);
 reader_perf_scope_end(&invalid);
 reader_perf_finish(true);
 EXPECT_MEMORY_READY(true);
#if CONFIG_READER_MEMORY
 reader_memory_stats_t memory={.font_source=123,.chapter=456,.prefetch_worker=1,.prefetch_stack_free=789};
 reader_perf_memory("present",&memory);
 reader_source_memory_stats_t source_memory={.txt_index=11,.epub_text=22,.zip_names=33,.open_files=1};
 reader_perf_source_memory("present",&source_memory);
#endif
 reader_perf_finish(true);assert(stops==1&&ends==1);
 now=90;reader_perf_begin("cancel");old=reader_perf_scope_begin(READER_PERF_LAYOUT);
 now=100;reader_perf_begin("replace");
 now=110;reader_perf_scope_end(&old);reader_perf_finish(false);assert(stops==3&&ends==3);
 monitor_owned=true;now=120;reader_perf_begin("external_monitor");
 now=130;reader_perf_finish(false);assert(stops==3&&monitor_owned);monitor_owned=false;
 assert(starts==4&&benches==4&&ends==4);
 puts("PASS: scopes, stale generations, negative stages, early return, incomplete operations and external heap monitor");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="reader-perf-") as folder:
        folder = Path(folder)
        for name, content in STUBS.items():
            path = folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        source, binary = folder / "test.c", folder / "test"
        source.write_text(CODE)
        flags = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                 "-fsanitize=address,undefined", "-I" + str(folder), "-I" + str(ROOT / "main"), "-I" + str(ROOT / "main/diagnostics")]
        subprocess.run(flags + [str(source), "-o", str(binary)], check=True)
        output = subprocess.check_output([str(binary)], text=True)
        rows = [dict(word.split("=", 1) for word in line.split()[1:])
                for line in output.splitlines() if line.startswith("READER_PERF ")]
        assert len(rows) == 4
        assert rows[0]["complete"] == "1" and rows[0]["total_us"] == "60"
        assert rows[0]["software_us"] == "30" and rows[0]["input_us"] == "10"
        assert rows[0]["action_us"] == "25" and rows[0]["parse_us"] == "5"
        assert rows[0]["source_us"] == "60" and rows[0]["display_us"] == "30"
        assert rows[0]["internal_min"] == "800" and rows[0]["psram_min"] == "1800"
        assert rows[1]["op"] == "cancel" and rows[1]["complete"] == "0"
        assert rows[2]["layout_us"] == "0" and rows[2]["id"] != rows[1]["id"]
        assert rows[3]["heap_local"] == "0" and rows[3]["internal_min"] == "0"
        memory_rows=[line for line in output.splitlines() if line.startswith("READER_MEMORY ")]
        assert len(memory_rows)==1 and "id=1 event=present" in memory_rows[0]
        assert "font_source=123" in memory_rows[0] and "chapter=456" in memory_rows[0]
        assert "prefetch_stack_free=789" in memory_rows[0] and "psram_free=2000" in memory_rows[0]
        source_rows=[line for line in output.splitlines() if line.startswith("READER_SOURCE_MEMORY ")]
        assert len(source_rows)==1 and "id=1 event=present" in source_rows[0]
        assert "txt_index=11" in source_rows[0] and "epub_text=22" in source_rows[0]
        assert "zip_names=33" in source_rows[0] and "open_files=1" in source_rows[0]
        print(output, end="")
        parsed = summary.parse_samples(output)
        assert summary.summarize(parsed, 20)["turn"]["enough_samples"] is False
        good = parsed[0]
        sequence = [dict(good, total_us=i, software_us=0, heap_local=0) for i in range(1, 21)]
        sequence.append(dict(good, complete=0, total_us=999999))
        report = summary.summarize(sequence, 20)["turn"]
        assert report["timing_us"]["total_us"] == {"p50": 10, "p95": 19, "max": 20}
        assert report["incomplete"] == 1 and "internal_min" not in report["memory_bytes"]
        for broken in (output.replace("v=1", "v=2", 1), output.replace("total_us=60", "total_us=-1", 1),
                       output.replace("input_us=10", "", 1), "boot only"):
            try:
                summary.parse_samples(broken)
                raise AssertionError("invalid input accepted")
            except ValueError:
                pass
        log, manifest, result = folder / "serial.log", folder / "manifest.json", folder / "report.json"
        log.write_text(output)
        manifest.write_text(json.dumps({"book_sha256": "a" * 64, "font_sha256": "b" * 64,
                                        "firmware_sha256": "c" * 64, "settings": {"size": 28}}))
        command = ["python3", str(ROOT / "tools/summarize_reader_perf.py"), str(log), "--scenario", "host-only",
                   "--manifest", str(manifest), "--output", str(result)]
        assert subprocess.run(command, check=False).returncode == 2
        assert json.loads(result.read_text())["operations"]["turn"]["complete"] == 1
        print("PASS: nearest-rank P50/P95, incomplete counts, invalid records, unavailable heap lows and sample gate")
        # 纯计时构建保持原始计时记录，且没有资源遍历符号。/ Timing-only builds retain identical timing records without resource walks.
        (folder / "sdkconfig.h").write_text("#define CONFIG_READER_PERF 1\n#define CONFIG_READER_MEMORY 0\n")
        subprocess.run(flags + [str(source), "-o", str(binary)], check=True)
        timing_only = subprocess.check_output([str(binary)], text=True)
        assert summary.parse_samples(timing_only) == parsed
        assert "READER_MEMORY " not in timing_only
        symbols = subprocess.check_output(["nm", str(binary)], text=True)
        assert "reader_perf_memory" not in symbols and "reader_perf_source_memory" not in symbols
        print("PASS: timing-only output equals resource-enabled timing and has no resource symbols")
        # 禁用构建不得产生测量符号。/ Disabled builds must emit no profiler symbols.
        (folder / "sdkconfig.h").write_text("#define CONFIG_READER_PERF 0\n")
        obj = folder / "disabled.o"
        subprocess.run(flags + ["-c", str(ROOT / "main/diagnostics/reader_perf.c"), "-o", str(obj)], check=True)
        symbols = subprocess.check_output(["nm", str(obj)], text=True)
        assert "reader_perf_" not in symbols
        print("PASS: disabled profiler has no symbols")


if __name__ == "__main__":
    main()
