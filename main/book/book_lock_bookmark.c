/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：透明PNG书封直接叠加在当前画面上；保留原画布比例。解码后的图层按PNG的mtime/尺寸缓存在TF卡，锁屏只做合成。
 * English: Composite a transparent PNG cover over the current frame, preserving canvas ratio. The decoded layer is cached on the SD card keyed by PNG mtime/size so a lock only composites.
 */
#include "book_lock_bookmark.h"
#include "book_cover.h"
#include "ui_image_dither.h"
#include "esp_heap_caps.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
// 计时日志：拆分探测/解码/去白/阴影/合成，宿主机测试走空实现但仍消费参数。
// Timing log: split probe/decode/white removal/shadow/blend; host tests get a no-op that still consumes its arguments.
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
#define LOCK_LOG(...) ESP_LOGI("lock", __VA_ARGS__)
#define LOCK_US() esp_timer_get_time()
#else
static void lock_trace(const char *fmt, ...) __attribute__((unused));
static void lock_trace(const char *fmt, ...) { (void)fmt; }
#define LOCK_LOG(...) lock_trace(__VA_ARGS__)
#define LOCK_US() ((int64_t)0)
#endif
#define LOCK_MS(from, to) (((to) - (from)) / 1000)

typedef struct {
    uint8_t *visited,*pending;
    uint16_t *rows;
    unsigned height,head,tail,count;
} white_flood_t;
static void white_flood_mark(white_flood_t *f,size_t at,unsigned y) {
    uint8_t bit=(uint8_t)(1u<<(at&7));
    if(f->visited[at>>3]&bit)return;
    f->visited[at>>3]|=bit;
    if(f->pending[y])return;
    f->pending[y]=1;f->rows[f->tail]=(uint16_t)y;
    f->tail=(f->tail+1)%f->height;++f->count;
}
// 不透明白底PNG才自动去白：四邻接连通到画布边缘，封闭白色图案保留。
// Remove only near-white regions connected to an opaque PNG's canvas edges by four-neighbor paths; preserve enclosed whites.
// 原始alpha优先，避免透明素材的白色主体被误删；工作区仅一位/像素和有界行队列。
// Original alpha takes priority, protecting white artwork in transparent assets; workspace is one bit/pixel plus a bounded row queue.
static bool remove_white_background(uint8_t *gray,uint8_t *alpha,unsigned w,unsigned h) {
    size_t count=(size_t)w*h;
    if(!gray||!alpha||!w||!h||w>684||h>1216)return false;
    for(size_t i=0;i<count;++i)if(alpha[i]!=255)return true;
    bool border=false;
    for(unsigned x=0;x<w;++x)if(gray[x]>=250||gray[(h-1)*w+x]>=250){border=true;break;}
    if(!border)for(unsigned y=0;y<h;++y)if(gray[(size_t)y*w]>=250||gray[(size_t)y*w+w-1]>=250){border=true;break;}
    if(!border)return true;
    size_t bits=(count+7)/8,queue_offset=(bits+h+1)&~(size_t)1;
    uint8_t *work=heap_caps_malloc(queue_offset+2*h,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!work)return false;
    memset(work,0,queue_offset);
    white_flood_t f={.visited=work,.pending=work+bits,.rows=(uint16_t *)(work+queue_offset),.height=h};
    for(unsigned x=0;x<w;++x){
        if(gray[x]>=250)white_flood_mark(&f,x,0);
        size_t at=(size_t)(h-1)*w+x;if(gray[at]>=250)white_flood_mark(&f,at,h-1);
    }
    for(unsigned y=1;y+1<h;++y){
        size_t at=(size_t)y*w;if(gray[at]>=250)white_flood_mark(&f,at,y);
        if(gray[at+w-1]>=250)white_flood_mark(&f,at+w-1,y);
    }
    while(f.count){
        unsigned y=f.rows[f.head];f.head=(f.head+1)%h;--f.count;f.pending[y]=0;
        size_t row=(size_t)y*w;
        for(unsigned x=0;x<w;){
            if(gray[row+x]<250){++x;continue;}
            unsigned left=x;bool reached=false;
            while(x<w&&gray[row+x]>=250){size_t at=row+x++;if(f.visited[at>>3]&(1u<<(at&7)))reached=true;}
            if(!reached)continue;
            for(unsigned xx=left;xx<x;++xx){
                size_t at=row+xx;f.visited[at>>3]|=(uint8_t)(1u<<(at&7));
                if(y&&gray[at-w]>=250)white_flood_mark(&f,at-w,y-1);
                if(y+1<h&&gray[at+w]>=250)white_flood_mark(&f,at+w,y+1);
            }
        }
    }
    for(size_t i=0;i<count;++i)if(f.visited[i>>3]&(1u<<(i&7)))gray[i]=alpha[i]=0;
    free(work);return true;
}
static uint8_t pixel(const uint8_t *fb,int x,int y) {
    int px=x,py=y;
    switch(epd_get_rotation()) {
        case EPD_ROT_PORTRAIT:px=epd_width()-y-1;py=x;break;
        case EPD_ROT_INVERTED_PORTRAIT:px=y;py=epd_height()-x-1;break;
        case EPD_ROT_INVERTED_LANDSCAPE:px=epd_width()-x-1;py=epd_height()-y-1;break;
        default:break;
    }
    return (epd_get_pixel(px,py,epd_width(),epd_height(),fb)>>4)*17u;
}
// 产品几何（1216x684 倒置竖屏）下，逻辑 (x,y) 落在物理行 683-x、列 y，字节地址只随 x 换行。
// 直接算字节地址省掉每像素两次旋转分支；其他几何仍走 pixel()/epd_draw_pixel()。
// At the product geometry a logical pixel lands on physical row 683-x, column y, so the byte
// address changes only with x; skip the two per-pixel rotation branches. Other geometries keep pixel()/epd_draw_pixel().
static bool s_fast_geometry;
static size_t s_fast_stride,s_fast_row;
static bool s_layer_cached;
bool book_lock_bookmark_cached(void) { return s_layer_cached; }
static void lock_fast_setup(void) {
    s_fast_geometry=epd_get_rotation()==EPD_ROT_INVERTED_PORTRAIT&&epd_width()==1216&&epd_height()==684;
    s_fast_stride=(size_t)epd_width()/2;
    s_fast_row=(size_t)epd_height()-1;
}
static unsigned fb_gray(const uint8_t *fb,int x,int y) {
    if(!s_fast_geometry)return pixel(fb,x,y);
    const uint8_t *p=fb+(s_fast_row-(size_t)x)*s_fast_stride+((unsigned)y>>1);
    return (((unsigned)y&1)?((unsigned)*p>>4):((unsigned)*p&0x0f))*17u;
}
static void fb_put(uint8_t *fb,int x,int y,uint8_t gray) {
    if(!s_fast_geometry){epd_draw_pixel(x,y,gray,fb);return;}
    uint8_t *p=fb+(s_fast_row-(size_t)x)*s_fast_stride+((unsigned)y>>1);
    if((unsigned)y&1)*p=(uint8_t)((*p&0x0f)|(gray&0xf0));
    else *p=(uint8_t)((*p&0xf0)|(gray>>4));
}

/* ---- 解码图层缓存 / Decoded layer cache ---- */
// 只缓存预乘灰阶+alpha两段平面的RLE，不缓存整帧；头部按路径、PNG的mtime/尺寸/旋转失效。
// Only the RLE of the two planes (premultiplied gray + alpha) is cached, never a frame; the header invalidates on path, PNG mtime/size/rotation.
// 路径也参与键：FatFs 时间戳只有2秒精度，同尺寸的批量导出可能同mtime，不能只看文件属性。
// The path takes part in the key: FatFs timestamps have two-second resolution, so same-size batch exports can share an mtime.
#ifndef LOCK_LAYER_ROOT
#define LOCK_LAYER_ROOT "/sdcard/.readpico"
#endif
#define LOCK_LAYER_DIR LOCK_LAYER_ROOT "/locks"
#define LOCK_LAYER_PATH LOCK_LAYER_DIR "/bookmark-layer.bin"
#define LOCK_LAYER_MAGIC UINT32_C(0x424b4c59)
#define LOCK_LAYER_VERSION 2u
#define LOCK_LAYER_STAGE 32768u
typedef struct {
    uint32_t magic,version,rotation,width,height;
    uint32_t path_hash;
    uint64_t file_size,file_mtime;
    uint32_t payload,checksum;
} layer_header_t;
static uint32_t layer_hash(const uint8_t *p,size_t n,uint32_t h) {
    while(n--)h=(h^*p++)*16777619u;
    return h;
}
static uint32_t layer_path_hash(const char *path) {
    return layer_hash((const uint8_t *)path,path?strlen(path):0,2166136261u);
}
// PackBits：0x00..0x7F 后跟 n+1 个原样字节，0x80..0xFF 后跟 1 字节重复 257-n 次。
// PackBits: 0x00..0x7F then n+1 literal bytes; 0x80..0xFF then one byte repeated 257-n times.
typedef struct { FILE *f; size_t used,written; uint32_t hash; bool ok; } rle_write_t;
// 32KB 暂存把每次 fwrite 从1KB提到半个簇，SD写入快数倍；只有一个写者，静态即可。
// A 32KB stage lifts each fwrite from 1KB to half a cluster, several times faster on SD; one writer only, so static is fine.
static uint8_t s_rle_stage[LOCK_LAYER_STAGE];
static bool rle_flush(rle_write_t *w) {
    if(!w->ok||!w->used)return w->ok;
    if(fwrite(s_rle_stage,1,w->used,w->f)!=w->used){w->ok=false;return false;}
    w->hash=layer_hash(s_rle_stage,w->used,w->hash);
    w->written+=w->used;w->used=0;return true;
}
static void rle_put(rle_write_t *w,uint8_t b) {
    if(!w->ok)return;
    s_rle_stage[w->used++]=b;
    if(w->used==LOCK_LAYER_STAGE)rle_flush(w);
}
static bool layer_encode(FILE *f,const uint8_t *src,size_t n,uint32_t *size,uint32_t *hash) {
    rle_write_t w={.f=f,.hash=2166136261u,.ok=true};
    size_t i=0;
    while(i<n) {
        size_t run=1;
        while(i+run<n&&src[i+run]==src[i]&&run<129)++run;
        if(run>=3){rle_put(&w,(uint8_t)(257-run));rle_put(&w,src[i]);i+=run;continue;}
        size_t start=i;
        while(i<n&&i-start<128) {
            if(i+2<n&&src[i]==src[i+1]&&src[i]==src[i+2])break;
            ++i;
        }
        rle_put(&w,(uint8_t)(i-start-1));
        for(size_t k=start;k<i;++k)rle_put(&w,src[k]);
    }
    if(!rle_flush(&w))return false;
    *size=(uint32_t)w.written;*hash=w.hash;return true;
}
static bool layer_decode(const uint8_t *src,size_t n,uint8_t *out,size_t out_n) {
    size_t i=0,o=0;
    while(i<n&&o<out_n) {
        uint8_t c=src[i++];
        if(c<0x80) {
            size_t k=(size_t)c+1;
            if(i+k>n||o+k>out_n)return false;
            memcpy(out+o,src+i,k);i+=k;o+=k;
        } else {
            size_t k=(size_t)(257-c);
            if(i>=n||o+k>out_n)return false;
            memset(out+o,src[i++],k);o+=k;
        }
    }
    return o==out_n&&i==n;
}
static bool layer_load(const char *path,unsigned w,unsigned h,uint8_t *dst) {
    struct stat st;
    if(!path||!dst||stat(path,&st))return false;
    FILE *f=fopen(LOCK_LAYER_PATH,"rb");
    if(!f)return false;
    layer_header_t head;bool ok=fread(&head,1,sizeof(head),f)==sizeof(head)&&
        head.magic==LOCK_LAYER_MAGIC&&head.version==LOCK_LAYER_VERSION&&
        head.rotation==(uint32_t)epd_get_rotation()&&head.width==w&&head.height==h&&
        head.path_hash==layer_path_hash(path)&&
        head.file_size==(uint64_t)st.st_size&&head.file_mtime==(uint64_t)st.st_mtime&&
        head.payload>0&&head.payload<=2u*w*h;
    uint8_t *payload=NULL;
    if(ok) {payload=heap_caps_malloc(head.payload,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);ok=payload!=NULL;}
    if(ok)ok=fread(payload,1,head.payload,f)==head.payload&&fgetc(f)==EOF;
    if(ok)ok=layer_hash(payload,head.payload,2166136261u)==head.checksum;
    if(ok)ok=layer_decode(payload,head.payload,dst,(size_t)w*h*2u);
    free(payload);fclose(f);return ok;
}
static bool layer_store(const char *path,unsigned w,unsigned h,const uint8_t *src) {
    struct stat st;
    if(!path||stat(path,&st))return false;
    (void)mkdir(LOCK_LAYER_ROOT,0775);(void)mkdir(LOCK_LAYER_DIR,0775);
    FILE *f=fopen(LOCK_LAYER_PATH ".tmp","wb");
    if(!f)return false;
    layer_header_t head={.magic=LOCK_LAYER_MAGIC,.version=LOCK_LAYER_VERSION,
        .rotation=(uint32_t)epd_get_rotation(),.width=w,.height=h,
        .path_hash=layer_path_hash(path),
        .file_size=(uint64_t)st.st_size,.file_mtime=(uint64_t)st.st_mtime};
    bool ok=fwrite(&head,1,sizeof(head),f)==sizeof(head);
    if(ok)ok=layer_encode(f,src,(size_t)w*h*2u,&head.payload,&head.checksum);
    // 载荷长度与校验只有编码完才知道，回填头部后改名，读到的永远是完整文件。
    // Payload size and checksum exist only after encoding; patch the header and rename so readers never see a partial file.
    if(ok)ok=fflush(f)==0&&fseek(f,0,SEEK_SET)==0&&fwrite(&head,1,sizeof(head),f)==sizeof(head)&&fflush(f)==0;
    if(fclose(f))ok=false;
    if(ok) {
        // FatFs 的 rename 遇到同名目标返回 FR_EXIST 而不是覆盖：必须先删旧文件。
        // FatFs rename returns FR_EXIST instead of overwriting: drop the old file first.
        (void)remove(LOCK_LAYER_PATH);
        ok=rename(LOCK_LAYER_PATH ".tmp",LOCK_LAYER_PATH)==0;
    }
    if(!ok)(void)remove(LOCK_LAYER_PATH ".tmp");
    if(!ok)LOCK_LOG("layer cache store failed");
    return ok;
}
// 以整个PNG画布居中等比缩放，684x1216时逐像素对齐，不识别或裁剪主体。
// Center-fit the entire PNG canvas; 684x1216 maps pixel-for-pixel, without subject detection or cropping.
static bool canvas_for(const char *path,unsigned *w,unsigned *h) {
    unsigned sw,sh;
    const char *ext=path?strrchr(path,'.'):NULL;
    if(!ext||strcasecmp(ext,".png")||!book_image_file_dimensions(path,true,&sw,&sh)||!sw||!sh)return false;
    unsigned cw=684,ch=(uint64_t)sh*cw/sw;
    if(ch>1216){ch=1216;cw=(uint64_t)sw*ch/sh;}
    if(!cw||!ch)return false;
    *w=cw;*h=ch;return true;
}
static bool lock_draw(uint8_t *fb,const char *path,bool store) {
    const int64_t lock_t0=LOCK_US();
    unsigned w,h;
    if(!fb||!canvas_for(path,&w,&h))return false;
    size_t count=(size_t)w*h;
    size_t psram_free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(psram_free<2*count+512u*1024u)return false;
    uint8_t *layers=heap_caps_malloc(2*count,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!layers)return false;
    const int64_t lock_t1=LOCK_US();
    uint8_t *alpha=layers+count;
    // 缓存命中直接得到图层，跳过解码PNG与去白底；未命中解码后顺手把缓存写出去。
    // A cache hit supplies the layers and skips the PNG decode and white removal; a miss decodes and then refreshes the cache.
    bool cached=layer_load(path,w,h,layers);
    s_layer_cached=cached;
    const int64_t lock_t2=LOCK_US();
    int64_t lock_t3=lock_t2,lock_t4=lock_t2,lock_t5=lock_t2,lock_t6=lock_t2,lock_t7=lock_t2;
    bool ok=true;
    if(!cached) {
        ok=book_png_file_layer(path,w,h,layers,alpha);
        lock_t3=LOCK_US();
        if(ok)ok=remove_white_background(layers,alpha,w,h);
        lock_t4=LOCK_US();
    }
    int ox=(684-(int)w)/2,oy=(1216-(int)h)/2;
    if(ok) {
        lock_fast_setup();
        // 小幅偏移的柔影只作用在非透明轮廓附近。/ A softly offset shadow affects only the nontransparent silhouette.
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            unsigned a=alpha[(size_t)y*w+x];if(!a||x+ox+4>=684||y+oy+6>=1216)continue;
            // 影子目标若被不透明封面整像素覆盖，合成时与影子无关，直接跳过。
            // A shadow target fully covered by the opaque cover is overwritten by the blend, so skip it.
            unsigned sx=x+4,sy=y+6;
            if(sx<w&&sy<h&&alpha[(size_t)sy*w+sx]==255)continue;
            int xx=ox+x+4,yy=oy+y+6;
            unsigned bg=fb_gray(fb,xx,yy);
            fb_put(fb,xx,yy,ui_image_dither_gray((bg*(255-a/5)+127)/255,xx,yy));
        }
        lock_t5=LOCK_US();
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            size_t at=(size_t)y*w+x;unsigned a=alpha[at];if(!a)continue;
            int xx=ox+x,yy=oy+y;
            unsigned value=layers[at]+(fb_gray(fb,xx,yy)*(255-a)+127)/255;
            if(value>255)value=255;
            fb_put(fb,xx,yy,ui_image_dither_gray(value,xx,yy));
        }
        lock_t6=LOCK_US();
        if(!cached&&store) {layer_store(path,w,h,layers);lock_t7=LOCK_US();}
        else lock_t7=lock_t6;
    }
    // 缓存命中时 png/white 为 0，load 是读SD+解RLE的耗时；未命中时 store 是首次写缓存。
    // On a hit png/white are zero and load is SD read plus RLE decode; on a miss store is the one-off cache write.
    LOCK_LOG("bookmark %ux%u ok=%d hit=%d psram=%uKB probe=%lld load=%lld png=%lld white=%lld shadow=%lld blend=%lld store=%lld total=%lld ms",
        w,h,(int)ok,(int)cached,(unsigned)(psram_free/1024),(long long)LOCK_MS(lock_t0,lock_t1),
        (long long)LOCK_MS(lock_t1,lock_t2),(long long)LOCK_MS(lock_t2,lock_t3),
        (long long)LOCK_MS(lock_t3,lock_t4),(long long)LOCK_MS(lock_t4,lock_t5),
        (long long)LOCK_MS(lock_t5,lock_t6),(long long)LOCK_MS(lock_t6,lock_t7),
        (long long)LOCK_MS(lock_t0,lock_t7));
    free(layers);return ok;
}
bool book_lock_bookmark_draw(uint8_t *fb,const char *png) { return lock_draw(fb,png,true); }
// 设置页逐张预览只读缓存、不回写：翻图不该每滑一次就写200KB。
// The settings picker only reads the cache: browsing images must not write 200 KB per swipe.
bool book_lock_bookmark_preview(uint8_t *fb,const char *png) { return lock_draw(fb,png,false); }
// 选图确认时预建图层缓存：换图后第一次锁屏不再等解码和去白底。
// Build the layer cache when an image is picked, so the first lock after a change never decodes.
void book_lock_bookmark_prepare(const char *png) {
    const int64_t t0=LOCK_US();
    unsigned w,h;
    if(!canvas_for(png,&w,&h))return;
    size_t count=(size_t)w*h;
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<2*count+512u*1024u)return;
    uint8_t *layers=heap_caps_malloc(2*count,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!layers)return;
    bool cached=layer_load(png,w,h,layers);
    const int64_t t1=LOCK_US();
    if(!cached) {
        uint8_t *alpha=layers+count;
        if(book_png_file_layer(png,w,h,layers,alpha)&&remove_white_background(layers,alpha,w,h))
            layer_store(png,w,h,layers);
    }
    const int64_t t2=LOCK_US();
    LOCK_LOG("bookmark prepare %ux%u hit=%d load=%lld build=%lld total=%lld ms",
        w,h,(int)cached,(long long)LOCK_MS(t0,t1),(long long)LOCK_MS(t1,t2),(long long)LOCK_MS(t0,t2));
    free(layers);
}
void book_lock_bookmark_preview_paint(uint8_t *fb,const uint8_t *canvas,EpdRect area) {
    if(!fb||!canvas||area.width<1||area.height<1)return;
    for(int y=0;y<area.height;++y)for(int x=0;x<area.width;++x)
        epd_draw_pixel(area.x+x,area.y+y,pixel(canvas,x*684/area.width,y*1216/area.height)&0xf0,fb);
}
