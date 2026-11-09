"""中文：书架真实服务的故障注入与资源、顺序断言，供页面宿主测试复用。
English: Fault injection and resource/order assertions for real shelf services in the page host test.
"""

shelf_hooks = r'''
static int test_nvs_handles,test_service_live;
static int test_frame_oom,test_grow_budget=-1,test_retry_oom,test_read_fault_depth;
static const char *test_stat_fault,*test_open_fault;
static bool test_trace;
static char test_events[128];
static book_shelf_retry_t **test_trace_retries;
static void *test_service_ptrs[64];
static void test_event(char event){
    if(!test_trace)return;
    size_t n=strlen(test_events);assert(n+1<sizeof(test_events));test_events[n]=event;test_events[n+1]=0;
}
static void *test_track(void *p){
    if(p){assert(test_service_live<64);test_service_ptrs[test_service_live++]=p;}
    return p;
}
static void test_service_free(void *p){
    if(p){int i=0;while(i<test_service_live && test_service_ptrs[i]!=p)++i;
        assert(i<test_service_live);test_service_ptrs[i]=test_service_ptrs[--test_service_live];}
    free(p);
}
static void *test_service_calloc(size_t n,size_t size,int caps){
    return test_frame_oom?NULL:test_track(heap_caps_calloc(n,size,caps));
}
static void *test_service_realloc(void *p,size_t size,int caps){
    if(test_grow_budget==0)return NULL;
    if(test_grow_budget>0)--test_grow_budget;
    return heap_caps_realloc(p,size,caps);
}
static void *test_service_malloc(size_t size,int caps){
    test_event('M');return test_retry_oom?NULL:test_track(heap_caps_malloc(size,caps));
}
static void *test_service_fallback(size_t size){
    test_event('m');return test_retry_oom?NULL:test_track(malloc(size));
}
static DIR *test_opendir(const char *path){
    if(test_open_fault && strstr(path,test_open_fault)){errno=EIO;return NULL;}
    DIR *dir=opendir(path);if(dir)++test_open_dirs;return dir;
}
static struct dirent *test_readdir(DIR *dir){
    if(test_read_fault_depth && test_open_dirs>=test_read_fault_depth){
        test_read_fault_depth=0;errno=EIO;return NULL;
    }
    return readdir(dir);
}
static int test_closedir(DIR *dir){assert(test_open_dirs>0);--test_open_dirs;return closedir(dir);}
static int test_stat(const char *path,struct stat *st){
    if(test_stat_fault && strstr(path,test_stat_fault)){errno=EIO;return -1;}
    return stat(path,st);
}
static int test_nvs_open(const char *ns,int mode,nvs_handle_t *out){
    int err=nvs_open(ns,mode,out);if(err==ESP_OK)++test_nvs_handles;return err;
}
static void test_nvs_close(nvs_handle_t h){assert(test_nvs_handles>0);--test_nvs_handles;nvs_close(h);}
static int test_service_delete(const char *path,bool *removed){
    test_event('D');
    if(test_trace){assert(test_trace_retries && book_shelf_retry_find(*test_trace_retries,path));}
    return book_store_delete(path,removed);
}
static int test_service_forget(const char *path){test_event('F');return book_progress_forget(path);}
static void test_service_notify(void){test_event('N');book_store_notify_changed();}
static unsigned test_service_revision(void){test_event('R');return book_store_revision();}
#define opendir test_opendir
#define readdir test_readdir
#define closedir test_closedir
#define stat(...) test_stat(__VA_ARGS__)
#define nvs_open test_nvs_open
#define nvs_close test_nvs_close
'''

service_hooks = r'''
#define heap_caps_calloc test_service_calloc
#define heap_caps_realloc test_service_realloc
#define heap_caps_malloc test_service_malloc
#define malloc test_service_fallback
#define free test_service_free
#define book_store_delete test_service_delete
#define book_progress_forget test_service_forget
#define book_store_notify_changed test_service_notify
#define book_store_revision test_service_revision
'''

service_unhooks = r'''
#undef heap_caps_calloc
#undef heap_caps_realloc
#undef heap_caps_malloc
#undef malloc
#undef free
#undef book_store_delete
#undef book_progress_forget
#undef book_store_notify_changed
#undef book_store_revision
#undef stat
'''

shelf_checks = r'''
static void test_manage_close(void){test_event('C');free_book();}
static void test_manage_discard(const char *path){test_event('P');pending_discard(path);}
static void test_manage_removed(void *ctx,const char *path){
    (void)ctx;test_event('A');assert(book_shelf_retry_find(*test_trace_retries,path)->entry.removed);
    assert(!pending_find(path));assert(s_store_revision==book_store_revision());
}
static void test_file(const char *path){FILE *f=fopen(path,"w");assert(f);fputs("x",f);assert(fclose(f)==0);}
static void test_scan_flags(const book_shelf_scan_t *scan,const char *root,bool truncated_expected,
                            bool unreadable_expected,int expected){
    *scan->catalog.count=0;
    bool truncated=false,unreadable=false,skipped=false;
    book_shelf_scan_dir(scan,root,false,0,0,&truncated,&unreadable,&skipped);
    assert(truncated==truncated_expected && unreadable==unreadable_expected && !skipped);
    assert(*scan->catalog.count==expected && !test_open_dirs && !test_service_live);
}
static void test_shelf_services(void){
    char root[96],chain[4][BOOK_STORE_PATH_MAX],path[340];
    snprintf(root,sizeof(root),"/tmp/shelf-service-%d",(int)getpid());assert(mkdir(root,0700)==0);
    strcpy(chain[0],root);
    for(int i=1;i<4;++i){snprintf(chain[i],sizeof(chain[i]),"%s/d",chain[i-1]);assert(mkdir(chain[i],0700)==0);}
    snprintf(path,sizeof(path),"%s/book.txt",chain[3]);test_file(path);
    strcpy(test_roots[0].path,root);app_ctx_t ctx={0};s_view=SHELF;
    // 导航中断必须交还各层目录与NVS，再使缓存失效，允许下次重扫。
    // Navigation cancellation closes every directory and NVS handle, invalidates cache and permits rescanning.
    for(int i=0;i<8;++i){
        test_nav_depth=3;s_shelf_cache_valid=true;scan_shelf(&ctx);
        assert(ctx.request_app && !s_shelf_cache_valid && s_scan_pending);
        assert(!test_open_dirs && !test_nvs_handles && !test_service_live);
        ctx.request_app=NULL;test_nav_depth=0;scan_shelf(&ctx);
        assert(s_count==1 && s_shelf_cache_valid && !test_open_dirs && !test_nvs_handles && !test_service_live);
    }
    book_shelf_scan_t scan=shelf_scan_context(&ctx);scan.cancel=NULL;
    test_frame_oom=1;test_scan_flags(&scan,root,true,false,0);test_frame_oom=0;
    test_read_fault_depth=3;test_scan_flags(&scan,root,false,true,0);
    test_open_fault="/d/d";test_scan_flags(&scan,root,false,true,0);test_open_fault=NULL;
    test_stat_fault="book.txt";test_scan_flags(&scan,root,false,true,0);test_stat_fault=NULL;
    test_scan_flags(&scan,root,false,false,1);
    // 介质消失后的读取错误与根目录不可打开都不得留下句柄。
    // Read failures after media loss and an unavailable root must leave no handles.
    test_read_fault_depth=1;test_scan_flags(&scan,root,false,true,0);
    test_scan_flags(&scan,"/nonexistent-shelf-service-root",false,true,0);
    assert(unlink(path)==0);for(int i=3;i>0;--i)assert(rmdir(chain[i])==0);
    for(int i=0;i<65;++i){snprintf(path,sizeof(path),"%s/%03d.txt",root,i);test_file(path);}
    snprintf(path,sizeof(path),"%s/unsupported.pdf",root);test_file(path);
    free(s_shelf);s_shelf=NULL;s_shelf_capacity=0;s_count=0;
    test_grow_budget=1;test_scan_flags(&scan,root,true,false,32);test_grow_budget=-1;
    test_scan_flags(&scan,root,false,false,65);
    // 历史与旧删除记录增长失败保留已有前缀；同路径重新出现仍保留清理标记。
    // Failed backfill/retry growth retains the prefix; a reappearing path retains its cleanup marker.
    snprintf(path,sizeof(path),"%s/000.txt",root);
    test_progress_count=1;strcpy(test_progress[0].path,path);test_progress[0].value.file_size=1;
    s_count=0;s_shelf_capacity=0;test_grow_budget=0;bool truncated=false;
    assert(!book_shelf_scan_backfill(&scan,&truncated) && truncated && !s_count && !test_nvs_handles);
    book_shelf_retry_t retry={.entry={.removed=true,.selected=true}};strcpy(retry.entry.path,path);
    truncated=false;book_shelf_scan_retries(&scan,&retry,&truncated);assert(truncated && !s_count);
    test_grow_budget=-1;test_scan_flags(&scan,root,false,false,65);
    int before=s_count;book_shelf_scan_retries(&scan,&retry,&truncated);assert(s_count==before);
    const book_shelf_retry_t *found=NULL;
    for(int i=0;i<s_count;++i)if(!strcmp(s_shelf[i].path,path)){
        assert(s_shelf[i].removed && !s_shelf[i].selected);found=&retry;
    }
    assert(found);test_progress_count=0;
    // 双路预留失败绝不调用删除；交还、预留、删除及元数据通知保持原顺序。
    // Both reservation failures must prevent deletion; handoff/reserve/delete/metadata notifications retain order.
    book_shelf_retry_t *retries=NULL;test_trace_retries=&retries;test_trace=true;
    book_shelf_management_t management={&retries,s_path,&s_store_revision,test_manage_close,test_manage_discard};
    book_shelf_entry_t entry={.selected=true};strcpy(entry.path,path);
    strcpy(s_path,path);test_retry_oom=1;bool removed=false;int deletes=test_delete_calls;
    test_events[0]=0;book_shelf_manage_result_t result=book_shelf_manage_one(&management,&entry,true,&removed,test_manage_removed,NULL);
    assert(result.reserve_failed && result.error==ESP_ERR_NO_MEM && !removed && !retries);
    assert(!strcmp(test_events,"CMm") && test_delete_calls==deletes && !s_path[0]);
    s_count=1;s_shelf[0]=entry;test_events[0]=0;
    book_shelf_batch_result_t batch=book_shelf_manage_batch(&management,s_shelf,&s_count,BOOK_SHELF_DELETE,shelf_hidden_save);
    assert(!batch.done && batch.failed==1 && s_count==1 && s_shelf[0].selected && test_delete_calls==deletes);
    assert(!strcmp(test_events,"Mm"));test_retry_oom=0;
    assert(pending_reserve(path));strcpy(s_path,path);test_removed=true;test_delete_error=ESP_FAIL;test_events[0]=0;
    result=book_shelf_manage_one(&management,&entry,true,&removed,test_manage_removed,NULL);
    assert(!result.reserve_failed && result.error==ESP_FAIL && removed && retries && retries->entry.removed);
    assert(!strcmp(test_events,"CMDPNRA") && test_delete_calls==deletes+1);
    test_events[0]=0;test_forget_error=ESP_FAIL;
    result=book_shelf_manage_one(&management,&entry,true,&removed,test_manage_removed,NULL);
    assert(result.error==ESP_FAIL && retries && !strcmp(test_events,"PF") && test_delete_calls==deletes+1);
    test_events[0]=0;test_forget_error=ESP_OK;
    result=book_shelf_manage_one(&management,&entry,true,&removed,test_manage_removed,NULL);
    assert(result.error==ESP_OK && !retries && !strcmp(test_events,"PF") && test_delete_calls==deletes+1);
    assert(!test_service_live);test_trace=false;test_trace_retries=NULL;
    test_delete_calls=0;test_delete_error=0;test_removed=false;test_notify_count=0;s_store_revision=0;
    free(s_shelf);s_shelf=NULL;s_count=0;s_shelf_capacity=0;
    for(int i=0;i<65;++i){snprintf(path,sizeof(path),"%s/%03d.txt",root,i);assert(unlink(path)==0);}
    snprintf(path,sizeof(path),"%s/unsupported.pdf",root);assert(unlink(path)==0);assert(rmdir(root)==0);
    s_query[0]=0;s_scan_pending=false;
    puts("book_shelf_services: PASS (cancel, handles, IO, OOM, retries, order)");
}
'''
