/*
 * SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：由 file_ui_host_test.py 拼接真实文件页后编译；不访问设备。
 * English: Compiled after real file-page functions by file_ui_host_test.py; no device access.
 */
static void fixture(bool home) {
    memset(s_files, 0, sizeof(s_files));
    s_page = 0; s_view = FILE_VIEW_LIST; s_scan_pending = s_sd_format_confirm = false;
    s_message[0] = s_sd_notice[0] = 0; s_pager_pressed = UI_PAGER_NONE;
    s_sd = (board_sd_info_t){.present=true, .mounted=true, .space_valid=true,
        .filesystem_bytes=32ULL*1024*1024*1024, .capacity_bytes=32ULL*1024*1024*1024,
        .free_bytes=26ULL*1024*1024*1024};
    const char *root[] = {"books", "fonts", "pictures", "mogo-e4p-update-1.0.3.bin",
        "随手记.txt", "阅读清单.txt", "随笔.txt", "音乐笔记.txt"};
    const char *books[] = {"瓦尔登湖.epub", "月亮与六便士.epub", "山茶文具店.epub", "小王子.epub",
        "城南旧事.txt", "人间草木.epub", "苏东坡传.epub", "夜航船.txt", "万物有灵且美.epub",
        "悉达多.epub", "长日将尽.epub", "草木春秋.epub"};
    s_folder = home ? -1 : 0; s_count = home ? 8 : 12;
    strcpy(s_dir, "/sdcard");
    for (int i=0; i<s_count; ++i) {
        copy_utf8(s_files[i].name, sizeof(s_files[i].name), home ? root[i] : books[i]);
        snprintf(s_files[i].path, sizeof(s_files[i].path), "/sdcard/%s%s", home ? "" : "books/", s_files[i].name);
        s_files[i].is_dir = home && i<3;
        s_files[i].size = home ? (i==3 ? 16777216 : (i+1)*6144) : (i+2)*512*1024;
    }
}
static app_redraw_t gesture(app_ctx_t *ctx,int type,int x,int y) {
    ui_gesture_event_t e={.type=type, .x0=x, .x=x, .y0=y, .y=y};
    return on_gesture(ctx,&e);
}
static void interaction(app_ctx_t *ctx) {
    fixture(true);
    assert(root_rows()==6 && file_pager_pages()==2);
    for (int row=0;row<6;++row) {
        EpdRect r=file_row_rect(row);
        assert(r.y==488+row*84 && r.height==84 && r.x==36 && r.width==612);
        assert(file_row_index(36,r.y)==row && file_row_index(647,r.y+83)==row);
        assert(file_row_index(35,r.y)<0 && file_row_index(648,r.y)<0);
    }
    assert(file_row_index(100,487)<0 && file_row_index(100,992)<0);
    assert(gesture(ctx,UI_GESTURE_TAP,120,991)==APP_REDRAW_PAGE && opened==1);
    assert(!strcmp(s_selected.name,s_files[5].name));
    gesture(ctx,UI_GESTURE_LONG_PRESS,120,950);
    assert(s_view==FILE_VIEW_ACTIONS && !strcmp(s_selected.name,s_files[5].name));
    gesture(ctx,UI_GESTURE_TAP,120,950); assert(action_calls==1);
    s_view=FILE_VIEW_RENAME;gesture(ctx,UI_GESTURE_TAP,120,950);assert(rename_calls==1);
    s_view=FILE_VIEW_LIST;
    gesture(ctx,UI_GESTURE_SWIPE_L,300,800);assert(s_page==1);
    gesture(ctx,UI_GESTURE_TAP,120,490);assert(opened==2 && !strcmp(s_selected.name,s_files[6].name));
    assert(file_row_index(100,656)<0);
    gesture(ctx,UI_GESTURE_LONG_PRESS,120,574);
    assert(s_view==FILE_VIEW_ACTIONS && !strcmp(s_selected.name,s_files[7].name));
    fixture(true);
    for(int i=0;i<3;++i) {
        route=-1;ctx->request_app=NULL;
        EpdRect r=home_transfer_rect(i);
        gesture(ctx,UI_GESTURE_TAP,r.x+r.width/2,302);assert(route==i && ctx->request_app==&app_transfer);
        route=-1;gesture(ctx,UI_GESTURE_LONG_PRESS,r.x+20,347);assert(route==-1 && s_view==FILE_VIEW_LIST);
    }
    const int gaps[]={214,249,273,406,422,460,487,992,1005};
    for(unsigned i=0;i<sizeof(gaps)/sizeof(gaps[0]);++i) {
        int before=opened;route=-1;
        gesture(ctx,UI_GESTURE_TAP,100,gaps[i]);
        assert(opened==before && route==-1 && !formats && !remounts);
    }
    s_sd.mounted=false;EpdRect storage=storage_card_rect();
    gesture(ctx,UI_GESTURE_LONG_PRESS,100,117);assert(!remounts);
    gesture(ctx,UI_GESTURE_TAP,storage.x,storage.y);assert(remounts==1 && s_scan_pending);
    s_sd.needs_format=true;s_scan_pending=false;
    gesture(ctx,UI_GESTURE_TAP,100,200);assert(s_sd_format_confirm && !formats);
    gesture(ctx,UI_GESTURE_TAP,100,249);assert(!s_sd_format_confirm && !formats);
    gesture(ctx,UI_GESTURE_TAP,647,212);gesture(ctx,UI_GESTURE_TAP,100,117);assert(formats==1);
    fixture(true);gesture(ctx,UI_GESTURE_TAP,100,530);
    assert(scans==1 && s_folder==3 && !strcmp(s_dir,"/sdcard/books"));
    fixture(false);
    for(int row=0;row<9;++row) {
        EpdRect r=file_row_rect(row);
        assert(r.y==215+row*86 && r.height==86);
        assert(file_row_index(100,r.y)==row && file_row_index(100,r.y+85)==row);
    }
    assert(file_row_index(100,214)<0 && file_row_index(100,989)<0);
    gesture(ctx,UI_GESTURE_LONG_PRESS,100,988);assert(s_view==FILE_VIEW_ACTIONS && !strcmp(s_selected.name,s_files[8].name));
    s_view=FILE_VIEW_LIST;gesture(ctx,UI_GESTURE_SWIPE_L,300,800);assert(s_page==1);
    gesture(ctx,UI_GESTURE_TAP,100,215);assert(!strcmp(s_selected.name,s_files[9].name));
    fixture(false);s_folder=3;s_view=FILE_VIEW_MOVE;
    for(int i=0;i<s_count;++i)s_files[i].is_dir=true;
    assert(visible_rows()==7 && file_pager_pages()==2 && file_row_index(100,829)==6 && file_row_index(100,830)<0);
    int before=scans;gesture(ctx,UI_GESTURE_TAP,100,829);assert(scans==before+1 && !strcmp(s_dir,s_files[6].path));
    gesture(ctx,UI_GESTURE_TAP,100,900);assert(cancels==1);
    gesture(ctx,UI_GESTURE_TAP,500,900);assert(moves==1);
    fixture(true);gesture(ctx,UI_GESTURE_TAP,100,1160);assert(nav==0);
    puts("PASS: six/nine/seven-row geometry, last rows, second pages, tap/hold, transfers, gaps, storage guards, move controls and modal routing");
}
static void snapshot(app_ctx_t *ctx,const char *dir,const char *name) {
    char path[2048];render(ctx,ctx->fb);
    snprintf(path,sizeof(path),"%s/%s.raw",dir,name);save(path,ctx->fb);
}
int main(int argc,char **argv) {
    assert(argc==2 && ttf_font_open_builtin()==ESP_OK);
    uint8_t *fb=malloc(684*1216/2);assert(fb);app_ctx_t ctx={.fb=fb};
    interaction(&ctx);
    for(font_percent=100;font_percent<=200;font_percent+=10) {
        char name[64];fixture(true);
        snprintf(name,sizeof(name),"home-%u",font_percent);snapshot(&ctx,argv[1],name);
        s_page=1;snprintf(name,sizeof(name),"page-2-%u",font_percent);snapshot(&ctx,argv[1],name);
        fixture(false);snprintf(name,sizeof(name),"directory-%u",font_percent);snapshot(&ctx,argv[1],name);
        copy_utf8(s_files[0].name,sizeof(s_files[0].name),"瓦尔登湖：一个人在森林里生活的完整记录与四季观察.epub");
        char shortened[160];file_list_name(shortened,&s_files[0],ui_text_effective_px(23),492);
        if(ui_text_fixed_width_px(ui_text_effective_px(23),s_files[0].name)>492)
            assert(strstr(shortened,"…"));
        assert(strrchr(shortened,'.') && !strcmp(strrchr(shortened,'.'),".epub"));
        assert(ui_text_fixed_width_px(ui_text_effective_px(23),shortened)<=492);
        snprintf(name,sizeof(name),"long-name-%u",font_percent);snapshot(&ctx,argv[1],name);
    }
    font_percent=120;fixture(false);s_count=0;snapshot(&ctx,argv[1],"empty-directory");
    fixture(true);s_count=0;s_sd.mounted=false;snapshot(&ctx,argv[1],"no-card");
    s_sd.needs_format=true;snapshot(&ctx,argv[1],"needs-format");
    s_sd_format_confirm=true;snapshot(&ctx,argv[1],"format-confirm");
    strcpy(s_sd_notice,"格式化失败：ESP_ERR_NOT_FINISHED");snapshot(&ctx,argv[1],"format-error");
    fixture(true);s_sd.space_valid=false;snapshot(&ctx,argv[1],"space-unavailable");
    fixture(true);s_folder=3;s_view=FILE_VIEW_MOVE;s_count=3;snapshot(&ctx,argv[1],"move");
    strcpy(s_message,"移动失败：ESP_ERR_INVALID_STATE");snapshot(&ctx,argv[1],"move-error");
    fixture(false);s_selected=s_files[0];s_view=FILE_VIEW_ACTIONS;snapshot(&ctx,argv[1],"actions");
    s_delete_confirm=true;snapshot(&ctx,argv[1],"delete-confirm");
    free(fb);
    printf("PASS: %u text/glyph bounds checks, 100..200%% font scaling, native product drawing\n",text_checks);
    return 0;
}
