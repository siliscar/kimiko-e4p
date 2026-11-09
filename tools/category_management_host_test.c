/* 中文：真实目录与事务式 NVS 替身，逐步注入失败并重启恢复。
 * English: Real directories and a transactional NVS fake, with stepwise failures and restart recovery. */
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "book_category_ops.h"
#include "book_bookmark_format.h"
#include "book_progress.h"
#include "book_title.h"
#include "nvs.h"

typedef struct { bool used; char key[16]; int type; size_t len; uint8_t data[640]; } record_t;
static record_t disk[5][64], ram[5][64];
static const char *const spaces[] = {"rp_books","rp_marks","rp_favs","rp_titles","rp_shelf"};
static int fail_at, writes;
static bool failing(void) { return fail_at > 0 && ++writes == fail_at; }
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h) {
    (void)mode;
    for(int i=0;i<5;++i)if(!strcmp(ns,spaces[i])) { *h=i+1;memcpy(ram[i],disk[i],sizeof(ram[i]));return ESP_OK; }
    return ESP_ERR_NVS_NOT_FOUND;
}
void nvs_close(nvs_handle_t h){(void)h;}
static record_t *record(nvs_handle_t h,const char *key,bool create) {
    for(int i=0;i<64;++i)if(ram[h-1][i].used&&!strcmp(ram[h-1][i].key,key))return &ram[h-1][i];
    if(create)for(int i=0;i<64;++i)if(!ram[h-1][i].used){record_t *r=&ram[h-1][i];memset(r,0,sizeof(*r));r->used=true;snprintf(r->key,16,"%s",key);return r;}
    return NULL;
}
static esp_err_t get(nvs_handle_t h,const char *key,void *out,size_t *len,int type) {
    record_t *r=record(h,key,false);if(!r)return ESP_ERR_NVS_NOT_FOUND;
    if(r->type!=type)return ESP_ERR_INVALID_STATE;
    if(out&&*len<r->len)return ESP_ERR_NVS_INVALID_LENGTH;
    if(out)memcpy(out,r->data,r->len);*len=r->len;return ESP_OK;
}
static esp_err_t set(nvs_handle_t h,const char *key,const void *data,size_t len,int type) {
    if(failing())return ESP_FAIL;
    assert(len<=640);record_t *r=record(h,key,true);assert(r);r->len=len;r->type=type;memcpy(r->data,data,len);return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *len){return get(h,key,out,len,1);}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t len){return set(h,key,data,len,1);}
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *len){return get(h,key,out,len,2);}
esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *s){return set(h,key,s,strlen(s)+1,2);}
esp_err_t nvs_get_u32(nvs_handle_t h,const char *key,uint32_t *v){size_t n=4;return get(h,key,v,&n,3);}
esp_err_t nvs_set_u32(nvs_handle_t h,const char *key,uint32_t v){return set(h,key,&v,4,3);}
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *v){size_t n=1;return get(h,key,v,&n,4);}
esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t v){return set(h,key,&v,1,4);}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key){if(failing())return ESP_FAIL;record_t *r=record(h,key,false);if(!r)return ESP_ERR_NVS_NOT_FOUND;r->used=false;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){if(failing())return ESP_FAIL;memcpy(disk[h-1],ram[h-1],sizeof(disk[h-1]));return ESP_OK;}
esp_err_t nvs_entry_find(const char *p,const char *ns,nvs_type_t t,nvs_iterator_t *it){(void)p;(void)ns;(void)t;*it=NULL;return ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_entry_next(nvs_iterator_t *it){(void)it;return ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t *info){(void)it;(void)info;return ESP_ERR_NVS_NOT_FOUND;}
void nvs_release_iterator(nvs_iterator_t it){(void)it;}

static void path(char out[288],const char *a,const char *b){assert(snprintf(out,288,"%s/%s",a,b)<288);}
static void file(const char *p){FILE *f=fopen(p,"wb");assert(f);assert(fwrite("book",1,4,f)==4);assert(!fclose(f));}
static bool exists(const char *p){struct stat st;return stat(p,&st)==0;}
static void key_for(char prefix,const char *p,char key[11]){uint32_t h=UINT32_C(2166136261);while(*p)h=(h^(unsigned char)*p++)*UINT32_C(16777619);snprintf(key,11,"%c_%08lx",prefix,(unsigned long)h);}
static void seed(const char *p) {
    book_progress_t value={.file_size=4,.chapter=3,.byte_off=99,.px=30,.pct=67};
    assert(book_progress_save(p,&value)==ESP_OK);assert(book_progress_set_last_path(p)==ESP_OK);
    assert(book_title_set(p,"保留书名")==ESP_OK);
    nvs_handle_t h;char key[11];reader_bookmarks_t marks={.magic=0x52504d32,.file_size=4,.count=2};
    strcpy(marks.path,p);marks.entries[0].byte_off=101;marks.entries[1].chapter=4;
    assert(nvs_open("rp_marks",1,&h)==ESP_OK);key_for('m',p,key);assert(nvs_set_blob(h,key,&marks,sizeof(marks))==ESP_OK);assert(!nvs_commit(h));
    assert(!nvs_open("rp_favs",1,&h));key_for('f',p,key);assert(!nvs_set_u8(h,key,1));assert(!nvs_commit(h));
    assert(!nvs_open("rp_shelf",1,&h));key_for('h',p,key);assert(!nvs_set_str(h,key,p));assert(!nvs_commit(h));
}
static void verify(const char *old,const char *next) {
    book_progress_t progress;assert(!book_progress_load(old,4,&progress));assert(book_progress_load(next,4,&progress));
    assert(progress.last_open_s==1&&progress.chapter==3&&progress.byte_off==99&&progress.pct==67);
    char last[288],title[128];assert(book_progress_last_path(last,sizeof(last))&&!strcmp(last,next));
    assert(book_title_get(next,title,sizeof(title))&&!strcmp(title,"保留书名"));assert(!book_title_get(old,title,sizeof(title)));
    nvs_handle_t h;char key[11];size_t n;reader_bookmarks_t marks;
    assert(!nvs_open("rp_marks",0,&h));key_for('m',next,key);n=sizeof(marks);assert(!nvs_get_blob(h,key,&marks,&n));
    assert(!strcmp(marks.path,next)&&marks.count==2&&marks.entries[0].byte_off==101&&marks.entries[1].chapter==4);
    key_for('m',old,key);n=sizeof(marks);assert(nvs_get_blob(h,key,&marks,&n)==ESP_ERR_NVS_NOT_FOUND);
    assert(!nvs_open("rp_favs",0,&h));uint8_t v=0;key_for('f',next,key);assert(!nvs_get_u8(h,key,&v)&&v==1);key_for('f',old,key);assert(nvs_get_u8(h,key,&v)==ESP_ERR_NVS_NOT_FOUND);
    assert(!nvs_open("rp_shelf",0,&h));key_for('h',next,key);n=sizeof(last);assert(!nvs_get_str(h,key,last,&n)&&!strcmp(last,next));
}
static int extra_fail,extra_count;
static esp_err_t extra(const char *a,const char *b,uint32_t n){assert(strcmp(a,b)&&n==4);++extra_count;if(extra_fail){extra_fail=0;return ESP_FAIL;}return ESP_OK;}
static void remove_tree(const char *p){DIR *d=opendir(p);if(!d){assert(!unlink(p));return;}struct dirent *e;while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char sub[288];path(sub,p,e->d_name);remove_tree(sub);}closedir(d);assert(!rmdir(p));}
static void failure_matrix(void) {
    unsigned injected=0;
    for(int failure=1;failure<=28;++failure) {
        char root[]="/tmp/kimiko-category-XXXXXX";assert(mkdtemp(root));char source[288],dest[288],old[288],next[288];
        path(source,root,"source");path(dest,root,"target");assert(!mkdir(source,0700));assert(!mkdir(dest,0700));
        path(old,source,"sample.epub");path(next,dest,"sample.epub");file(old);memset(disk,0,sizeof(disk));fail_at=0;seed(old);
        book_progress_watch_t *watch=book_progress_watch_create(old);assert(watch);
        writes=0;fail_at=failure;esp_err_t result=book_category_move(root,old,dest,extra);fail_at=0;
        assert(exists(old)!=exists(next));
        if(result!=ESP_OK) {
            ++injected;
            assert(book_category_pending(root));
            // 模拟重启丢弃所有未提交写入，再按卡上意图继续。
            // Simulate restart by dropping every uncommitted write, then replay the card intent.
            memset(ram,0,sizeof(ram));assert(book_category_recover(root,extra)==ESP_OK);
        }
        assert(!book_category_pending(root)&&exists(next)&&!exists(old));verify(old,next);
        assert(book_progress_watch_invalidated(watch));book_progress_watch_destroy(watch);remove_tree(root);
    }
    assert(injected==22);
}
int main(void) {
    assert(book_category_name_valid("文学")&&book_category_name_valid("Science 2026"));
    const char *invalid[]={"",".","..",".hidden"," leading","trailing ","trailing.","a/b","a\\b","bad:name","未分类","全部书籍","\xc0\xaf"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(*invalid);++i)assert(!book_category_name_valid(invalid[i]));
    char long_name[82];memset(long_name,'a',81);long_name[81]=0;assert(!book_category_name_valid(long_name));
    failure_matrix();
    char root[]="/tmp/kimiko-category-XXXXXX";assert(mkdtemp(root));char a[288],b[288],old[288],next[288];
    memset(disk,0,sizeof(disk));assert(!book_category_create(root,"文学",a));assert(exists(a));assert(book_category_create(root,"文学",b)==ESP_ERR_INVALID_STATE);
    assert(!book_category_rename(root,a,"文学",extra)&&!book_category_pending(root));
    assert(book_category_rename(NULL,a,"新名称",extra)==ESP_ERR_INVALID_ARG);
    assert(book_category_plan_move(root,NULL,root,next)==ESP_ERR_INVALID_ARG);
    book_category_result_t empty_result;
    assert(book_category_remove(root,a,"/outside-root",&empty_result,extra)==ESP_ERR_INVALID_ARG&&exists(a));
    assert(!book_category_create(root,"目标",b));path(old,a,"a.txt");file(old);seed(old);
    // 目标文件和目标历史记录均不能被覆盖。/ Neither destination files nor destination history may be overwritten.
    path(next,b,"a.txt");file(next);assert(book_category_move(root,old,b,extra)==ESP_ERR_INVALID_STATE);assert(exists(old)&&exists(next));assert(!unlink(next));
    assert(!book_title_set(next,"旧目标"));assert(book_category_move(root,old,b,extra)==ESP_ERR_INVALID_STATE);assert(exists(old));assert(!book_title_clear(next));
    extra_fail=1;assert(book_category_move(root,old,b,extra)!=ESP_OK);assert(book_category_pending(root)&&exists(next));
    assert(!book_category_recover(root,extra));verify(old,next);
    // 非书籍和子目录阻止整类删除，所有书籍留在原位。/ Non-books and subfolders block category removal before any book moves.
    char temp[288];path(old,a,"left.epub");file(old);path(temp,a,"notes.json");file(temp);book_category_result_t result;
    assert(book_category_remove(root,a,root,&result,extra)==ESP_ERR_NOT_SUPPORTED&&exists(old));assert(!unlink(temp));
    path(temp,a,"nested");assert(!mkdir(temp,0700));assert(book_category_remove(root,a,root,&result,extra)==ESP_ERR_NOT_SUPPORTED&&exists(old));
    char nested[288];path(nested,temp,"deep.txt");file(nested);
    // 改名保留嵌套结构，日志恢复不能重复丢掉元数据。/ Renames preserve nesting, with retryable metadata migration.
    memset(disk,0,sizeof(disk));seed(nested);char renamed[288];path(renamed,root,"小说");path(next,renamed,"nested/deep.txt");
    extra_fail=1;assert(book_category_rename(root,a,"小说",extra)!=ESP_OK);assert(!exists(a)&&exists(renamed));
    assert(!book_category_recover(root,extra));verify(nested,next);
    assert(book_category_remove(root,renamed,b,&result,extra)==ESP_ERR_NOT_SUPPORTED);
    // 平铺分类删除只转移文件，空分类也可直接删除。/ Flat removal transfers files only; empty categories can also be removed.
    char flat[288];assert(!book_category_create(root,"平铺",flat));path(old,flat,"one.txt");file(old);memset(disk,0,sizeof(disk));seed(old);path(next,root,"one.txt");
    assert(!book_category_remove(root,flat,root,&result,extra));assert(result.removed&&result.moved==1&&exists(next)&&!exists(flat));verify(old,next);
    assert(!book_category_create(root,"空分类",flat));assert(!book_category_remove(root,flat,root,&result,extra)&&result.removed&&result.moved==0);
    // 旧版本进度和单书签迁移后仍可识别，不虚构最近排序。
    // Legacy progress and single bookmarks remain readable without inventing recency.
    assert(!book_category_create(root,"旧记录",flat));path(old,flat,"legacy.txt");file(old);memset(disk,0,sizeof(disk));seed(old);
    nvs_handle_t h;char key[11];uint8_t raw[640];size_t length=sizeof(raw);
    assert(!nvs_open("rp_books",1,&h));key_for('b',old,key);assert(!nvs_get_blob(h,key,raw,&length));raw[3]=1;
    memset(raw+16,0,4);assert(!nvs_set_blob(h,key,raw,length)&&!nvs_commit(h));
    legacy_reader_bookmark_t legacy={.magic=0x52504d31,.file_size=4,.chapter=3,.byte_off=99};strcpy(legacy.path,old);
    assert(!nvs_open("rp_marks",1,&h));key_for('m',old,key);assert(!nvs_set_blob(h,key,&legacy,sizeof(legacy))&&!nvs_commit(h));
    path(next,b,"legacy.txt");assert(!book_category_move(root,old,b,extra));
    book_progress_t progress;assert(book_progress_load(next,4,&progress)&&progress.last_open_s==0&&progress.byte_off==99);
    assert(!nvs_open("rp_marks",0,&h));key_for('m',next,key);length=sizeof(legacy);assert(!nvs_get_blob(h,key,&legacy,&length));
    assert(legacy.magic==0x52504d31&&!strcmp(legacy.path,next)&&legacy.chapter==3&&legacy.byte_off==99);
    // 深度超限在改目录前拒绝，保留原结构且不留下意图。
    // Excess depth is refused before any folder rename, preserving the tree without intent.
    char deep[288],child[288];assert(!book_category_create(root,"深目录",flat));strcpy(deep,flat);
    for(int i=0;i<13;++i){path(child,deep,"d");assert(!mkdir(child,0700));strcpy(deep,child);}
    assert(book_category_rename(root,flat,"不能改名",extra)==ESP_ERR_INVALID_SIZE&&exists(flat)&&!book_category_pending(root));
    // 坏日志禁止新写入；不会据此猜测路径或删除文件。/ Corrupt journals block mutations without guessing paths or deleting files.
    path(temp,root,".kimiko-category-op");file(temp);assert(book_category_recover(root,extra)==ESP_ERR_INVALID_STATE);
    assert(book_category_create(root,"禁止写入",flat)==ESP_ERR_INVALID_STATE);assert(exists(next));
    remove_tree(root);puts("category management: PASS (22 write/commit/erase failures, recovery, metadata, collisions, nested rename, non-destructive removal)");
}
