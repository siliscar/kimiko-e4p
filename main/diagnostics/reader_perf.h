/*
 * 中文：仅诊断构建启用的阅读操作与阶段测量；不决定页面或刷新行为。
 * English: Diagnostic-only reader operation and stage timing; never selects UI or refresh behavior.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

typedef enum {
    READER_PERF_ACTION,
    READER_PERF_SOURCE,
    READER_PERF_READ,
    READER_PERF_PARSE,
    READER_PERF_LAYOUT,
    READER_PERF_PAINT,
    READER_PERF_DISPLAY,
    READER_PERF_JOIN,
    READER_PERF_LOCK,
    READER_PERF_STAGE_COUNT
} reader_perf_stage_t;

#if CONFIG_READER_PERF
typedef struct {
    reader_perf_stage_t stage; ///< 阶段 / Stage
    int64_t started; ///< 单调时钟 / Monotonic clock
    unsigned generation; ///< 操作代次，防止旧任务写入新样本 / Operation generation excludes stale work
} reader_perf_scope_t;

/// UI 线程登记输入处理起点。/ Mark input processing start on the UI thread.
void reader_perf_input_begin(void);
/// UI 线程开始一次操作，未呈现的旧操作记为未完成。/ Begin on the UI thread; an unpresented prior operation is incomplete.
void reader_perf_begin(const char *operation);
/// 所有借用字体的任务交还后，由 UI 线程完成并输出一条样本。/ Finish and emit on the UI thread after all font borrowers have returned.
void reader_perf_finish(bool presented);
/// 获取可跨线程累计的阶段计时。/ Start a stage that may accumulate across tasks.
reader_perf_scope_t reader_perf_scope_begin(reader_perf_stage_t stage);
/// 自动交还阶段计时；忽略旧代次。/ End a scope automatically, ignoring stale generations.
void reader_perf_scope_end(reader_perf_scope_t *scope);

#define READER_PERF_SCOPE(stage_) \
    reader_perf_scope_t reader_perf_scope_ __attribute__((cleanup(reader_perf_scope_end))) = \
        reader_perf_scope_begin(stage_)
#endif
