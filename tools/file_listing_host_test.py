"""中文：用真实目录测试文件列表扩容、筛选、分页与失败清理。
English: Test real-directory listing growth, filtering, paging and failure cleanup.
"""
from pathlib import Path
import re
import subprocess
import tempfile

from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/apps/app_files.c"
source = SOURCE.read_text()

unit = r'''
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define UI_PAGER_NONE -1
typedef struct {int unused;} app_ctx_t;
typedef struct {bool mounted;} board_sd_info_t;
static bool mounted=true,fail_stat;
static int open_error,read_error_after=-1,read_calls,open_dirs;
static size_t memory_limit=SIZE_MAX,live_bytes,alloc_calls;
static void *allocation;
static void *heap_caps_realloc(void *p,size_t bytes,int caps){
 assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)&&p==allocation);
 ++alloc_calls;
 if(bytes>memory_limit)return NULL;
 void *next=realloc(p,bytes);
 if(next){allocation=next;live_bytes=bytes;}
 return next;
}
static void heap_caps_free(void *p){
 if(!p)return;
 assert(p==allocation);free(p);allocation=NULL;live_bytes=0;
}
static DIR *test_opendir(const char *path){
 if(open_error){errno=open_error;return NULL;}
 DIR *dir=opendir(path);if(dir)++open_dirs;return dir;
}
static struct dirent *test_readdir(DIR *dir){
 if(read_error_after>=0&&read_calls++>=read_error_after){errno=EIO;return NULL;}
 return readdir(dir);
}
static int test_closedir(DIR *dir){assert(open_dirs>0);--open_dirs;return closedir(dir);}
static int test_stat(const char *path,struct stat *st){if(fail_stat){errno=EIO;return -1;}return stat(path,st);}
#define opendir test_opendir
#define readdir test_readdir
#define closedir test_closedir
#define stat(path,st) test_stat(path,st)
static void board_sd_get_info(board_sd_info_t *info){info->mounted=mounted;}
static int64_t esp_timer_get_time(void){return 1000000;}
static void ui_keyboard_end(void){}
static const char *root;
static const char *folder_root(int folder){(void)folder;return root;}
static int s_count,s_folder,s_page,s_counts[3],s_pager_pressed;
static bool s_scan_pending,s_delete_confirm,s_pager_paint_pending;
static int64_t s_scan_retry_ms;
static char s_message[96];
static board_sd_info_t s_sd;
'''
unit += re.search(r"^typedef struct .* file_item_t;", source, re.M)[0] + "\n"
unit += re.search(r"^typedef enum .* file_view_t;", source, re.M)[0] + "\n"
unit += re.search(r"^#define FILE_ROWS .*", source, re.M)[0] + "\n"
unit += re.search(r"^#define FILE_SCAN_RETRY_MS .*", source, re.M)[0] + "\n"
unit += "static file_item_t *s_files;\nstatic size_t s_files_capacity;\nstatic file_view_t s_view;\n"
for name in ("files_alloc", "files_free", "supported", "compare_files", "scan_folder",
             "visible_rows", "root_rows", "file_pager_pages", "files_before_lock",
             "files_exit", "on_media_lost"):
    unit += function(name, SOURCE) + "\n"
unit += r'''
static void check_listing(int count,int directories){
 assert(s_count==count&&s_page==0&&!s_message[0]&&!s_scan_retry_ms&&!open_dirs);
 assert(s_files_capacity>=(size_t)count);
 if(!count){assert(!s_files&&!live_bytes);return;}
 for(int i=0;i<count;++i){
  assert(s_files[i].is_dir==(i<directories));
  assert(strstr(s_files[i].path,root)==s_files[i].path);
  if(i)assert(compare_files(&s_files[i-1],&s_files[i])<=0);
 }
 int rows=s_folder<0?root_rows():visible_rows(),seen=0;
 assert(file_pager_pages()==1+(count-1)/rows);
 for(int page=0;page<file_pager_pages();++page)
  for(int row=0;row<rows&&page*rows+row<count;++row){
   int index=page*rows+row;assert(index==seen++&&s_files[index].name[0]);
  }
 assert(seen==count);
}
static void assert_released(void){assert(!s_files&&!s_files_capacity&&!s_count&&!live_bytes&&!open_dirs);}
int main(int argc,char **argv){
 assert(argc==7);app_ctx_t ctx={0};root=argv[1];s_folder=3;s_view=FILE_VIEW_LIST;
 scan_folder(3);check_listing(4659,257);
 assert(alloc_calls<20&&s_files_capacity>96);
 file_item_t selected=s_files[1000];
 assert(!selected.is_dir&&selected.name[0]);
 s_folder=-1;check_listing(4659,257);
 s_folder=0;scan_folder(0);check_listing(4129,0);assert(s_counts[0]==4129);
 s_folder=1;scan_folder(1);check_listing(133,0);assert(s_counts[1]==133);
 s_folder=2;scan_folder(2);check_listing(139,0);assert(s_counts[2]==139);
 s_folder=3;s_view=FILE_VIEW_MOVE;scan_folder(3);check_listing(257,257);
 assert(!selected.is_dir&&selected.name[0]);
 s_view=FILE_VIEW_LIST;
 for(int i=2;i<=4;++i){root=argv[i];scan_folder(3);check_listing(94+i,0);}
 root=argv[5];scan_folder(3);check_listing(0,0);
 root=argv[6];scan_folder(3);check_listing(1,0);assert(s_files[0].size==7);

 // 大目录转小目录应释放旧容量；预留扩容失败时仍可按精确数量完成。
 // Shrink on navigation; retry without slack if the exact count still fits.
 root=argv[1];scan_folder(3);size_t large=live_bytes;
 root=argv[6];scan_folder(3);assert(live_bytes<large&&s_count==1);
 root=argv[3];memory_limit=97*sizeof(file_item_t);scan_folder(3);check_listing(97,0);
 assert(s_files_capacity==97);
 memory_limit=96*sizeof(file_item_t);scan_folder(3);
 assert_released();assert(strstr(s_message,"内存不足")&&!s_scan_retry_ms);
 memory_limit=0;s_folder=0;s_counts[0]=97;scan_folder(0);
 assert_released();assert(strstr(s_message,"内存不足")&&!s_counts[0]);
 root=argv[5];scan_folder(3);check_listing(0,0);
 memory_limit=SIZE_MAX;root=argv[3];scan_folder(3);check_listing(97,0);

 // 目录打不开、卡掉线和扫描读错不保留旧列表，读错仍按原策略重试。
 // Open/media/read failures never retain the old list; IO retry policy is unchanged.
 open_error=ENOENT;scan_folder(3);assert_released();assert(s_message[0]&&!s_scan_retry_ms);
 open_error=EIO;scan_folder(3);assert_released();assert(s_message[0]&&s_scan_retry_ms==2500);
 open_error=0;read_error_after=0;read_calls=0;scan_folder(3);
 assert_released();assert(s_scan_retry_ms==2500);
 read_error_after=-1;scan_folder(3);check_listing(97,0);
 mounted=false;scan_folder(3);assert_released();assert(s_message[0]&&!s_scan_retry_ms);
 mounted=true;root=argv[1];fail_stat=true;s_folder=3;s_view=FILE_VIEW_MOVE;
 scan_folder(3);assert(s_count==257&&!open_dirs&&s_scan_retry_ms==2500);
 for(int i=0;i<s_count;++i)assert(s_files[i].is_dir);
 fail_stat=false;s_view=FILE_VIEW_LIST;scan_folder(3);check_listing(4659,257);
 s_page=3;files_exit(&ctx);assert_released();assert(s_page==0);
 scan_folder(3);check_listing(4659,257);s_scan_pending=true;s_page=3;
 on_media_lost(&ctx);assert_released();assert(s_folder==-1&&!s_scan_pending&&s_page==0);
 for(int i=0;i<3;++i)assert(!s_counts[i]);
 size_t before=alloc_calls;
 assert(!files_alloc(SIZE_MAX)&&!files_alloc((size_t)INT_MAX)&&alloc_calls==before);
 assert_released();
 puts("PASS: real 96/97/98 and 4659-entry directories, category/move filtering, all pages, growth fallback, OOM, IO errors, exit and media cleanup");
 return 0;
}
'''

assert ".on_exit = files_exit" in source and ".on_media_lost = on_media_lost" in source
with tempfile.TemporaryDirectory(dir=ROOT / "build") as folder:
    work = Path(folder)
    directories = [work / name for name in ("large", "96", "97", "98", "empty", "one")]
    for directory in directories:
        directory.mkdir()
    large = directories[0]
    for index in range(257):
        (large / f"dir-{index:04d}").mkdir()
    for extension, count in (("txt", 4096), ("EPUB", 33), ("png", 129),
                             ("JPEG", 4), ("ttf", 130), ("OTF", 9), ("bin", 1)):
        for index in range(count):
            (large / f"item-{index:05d}.{extension}").touch()
    (large / ".hidden.txt").touch()
    (large / "._metadata").touch()
    (large / ("x" * 128 + ".txt")).touch()
    for directory, count in zip(directories[1:4], (96, 97, 98)):
        for index in range(count):
            (directory / f"item-{index:03d}.txt").touch()
    (directories[-1] / "single.txt").write_bytes(b"content")
    source_file, binary = work / "test.c", work / "test"
    source_file.write_text(unit)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(source_file), "-o", str(binary)], check=True)
    subprocess.run([str(binary), *(str(directory) for directory in directories)], check=True)
