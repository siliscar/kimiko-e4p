"""中文：宿主测试借用真实封面模块，只有工作队列替身，不复制状态逻辑。
English: Host tests use the real cover module, with only worker-queue doubles and no copied state logic.
"""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]


def declarations():
    return '#include "' + str(ROOT / 'main/book/book_shelf_covers.h') + '''"
static book_shelf_covers_t s_shelf_covers;
#define s_covers s_shelf_covers.slots
#define s_cover_generation s_shelf_covers.generation
#define s_cover_pending_mask s_shelf_covers.pending
#define s_cover_dirty_mask s_shelf_covers.dirty
#define s_cover_present_mask s_shelf_covers.presenting
#define s_cover_warm_mask s_shelf_covers.warm
#define s_cover_warm_after_ms s_shelf_covers.warm_after_ms
'''


def implementation(jobs=True):
    stubs = '''
unsigned book_cover_jobs_begin(void) { return 1; }
void book_cover_jobs_cancel(unsigned generation) { (void)generation; }
bool book_cover_jobs_submit(unsigned generation, unsigned row, const book_cover_job_t *request) {
    (void)generation; (void)row; (void)request; return false;
}
int book_cover_jobs_take(unsigned generation, unsigned row, book_cover_result_t *result) {
    (void)generation; (void)row; (void)result; return -1;
}
''' if jobs else ''
    return stubs + '\n#include "' + str(ROOT / 'main/book/book_shelf_covers.c') + '"\n'
