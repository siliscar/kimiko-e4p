/* SPDX-License-Identifier: Apache-2.0
 * 中文：书籍封面选择、自动封面与持久缓存；通用解码由 image 模块负责。
 * English: Book cover selection, generated covers and persistent caches; image owns generic decoding.
 */
#include "book_cover.h"
#include "book_cover_auto.h"
#include "book_metadata.h"
#include "book_title.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"

bool book_cover_thumbnail(const uint8_t *data, size_t size, bool png,
                          uint8_t out[BOOK_COVER_W * BOOK_COVER_H]) {
    return book_image_grayscale(data, size, png, BOOK_COVER_W, BOOK_COVER_H, out);
}

/* ---- 共享封面缓存 / Shared cover cache ---- */

#define COVER_CACHE_MAGIC UINT32_C(0x52435041)
#define COVER_CACHE_SCHEMA 2u

typedef struct {
    uint32_t magic, schema, template_version, width, height;
    uint64_t file_size;
    int64_t modified;
    uint64_t path_hash, text_hash;
} cover_cache_header_t;

static uint64_t cover_hash(uint64_t hash, const char *text) {
    if (!text) text = "";
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    return (hash ^ 0xffu) * UINT64_C(1099511628211);
}

static bool cover_cache_key(const char *source, const char *title, const char *author,
                            char *path, size_t cap, cover_cache_header_t *header) {
    struct stat st;
    if (stat(source, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) return false;
    uint64_t path_hash = cover_hash(UINT64_C(14695981039346656037), source);
    uint64_t text_hash = cover_hash(cover_hash(UINT64_C(14695981039346656037), title), author);
    *header = (cover_cache_header_t){
        .magic = COVER_CACHE_MAGIC, .schema = COVER_CACHE_SCHEMA,
        .template_version = BOOK_AUTO_COVER_VERSION,
        .width = BOOK_COVER_W, .height = BOOK_COVER_H,
        .file_size = (uint64_t)st.st_size, .modified = (int64_t)st.st_mtime,
        .path_hash = path_hash, .text_hash = text_hash,
    };
    return snprintf(path, cap, "/sdcard/.readpico/covers/%016llx-%ux%u.bin",
                    (unsigned long long)path_hash, BOOK_COVER_W, BOOK_COVER_H) < (int)cap;
}

static bool cover_cache_read(const char *path, const cover_cache_header_t *expected,
                             uint8_t out[BOOK_COVER_W * BOOK_COVER_H]) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    cover_cache_header_t found;
    bool ok = fread(&found, 1, sizeof(found), file) == sizeof(found) &&
              found.magic == expected->magic && found.schema == expected->schema &&
              found.template_version == expected->template_version &&
              found.width == expected->width && found.height == expected->height &&
              found.file_size == expected->file_size && found.modified == expected->modified &&
              found.path_hash == expected->path_hash && found.text_hash == expected->text_hash &&
              fread(out, 1, BOOK_COVER_W * BOOK_COVER_H, file) == BOOK_COVER_W * BOOK_COVER_H &&
              fgetc(file) == EOF;
    fclose(file);
    return ok;
}

static void cover_cache_write(const char *path, const cover_cache_header_t *header,
                              const uint8_t out[BOOK_COVER_W * BOOK_COVER_H]) {
    if (mkdir("/sdcard/.readpico", 0777) && errno != EEXIST) return;
    if (mkdir("/sdcard/.readpico/covers", 0777) && errno != EEXIST) return;
    char temp[128];
    if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int)sizeof(temp)) return;
    FILE *file = fopen(temp, "wb");
    if (!file) return;
    bool ok = fwrite(header, 1, sizeof(*header), file) == sizeof(*header) &&
              fwrite(out, 1, BOOK_COVER_W * BOOK_COVER_H, file) == BOOK_COVER_W * BOOK_COVER_H;
    if (fclose(file)) ok = false;
    if (ok) {
        remove(path);
        if (!rename(temp, path)) return;
    }
    remove(temp);
}

static bool book_cover_load_gray_impl(const char *path, const char *title, const char *author,
                          uint8_t out[BOOK_COVER_W * BOOK_COVER_H],
                          bool allow_decode, bool *pending, bool allow_auto, bool *auto_cache) {
    if (pending) *pending = false;
    if (!path || !*path || !out) return false;
    char canonical_title[256];
    if (book_title_from_path(path, canonical_title, sizeof(canonical_title))) title = canonical_title;
    else if (!title || !*title) return false;
    char cache_path[120];
    cover_cache_header_t key;
    bool cacheable = cover_cache_key(path, title, author, cache_path, sizeof(cache_path), &key);
    if (!cacheable) return false;
    if (cover_cache_read(cache_path, &key, out)) return true;
    if (!allow_decode) { if (pending) *pending = true; return false; }

    bool good = false;
    bool cache_result = true;
    if (book_metadata_supported(path)) {
        uint8_t *data = NULL;
        size_t size = 0;
        bool png = false;
        size_t free_bytes=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        size_t budget=free_bytes>2u*1024u*1024u?free_bytes-2u*1024u*1024u:0;
        esp_err_t err = budget ? book_metadata_cover_bounded(path,&data,&size,&png,budget) : ESP_ERR_NO_MEM;
        if (err == ESP_OK) good = book_cover_thumbnail(data, size, png, out);
        free(data);
        if (err == ESP_ERR_NO_MEM) cache_result = false;
    }
    if (!good && !allow_auto) {
        if (pending) *pending = true;
        if (auto_cache) *auto_cache = cache_result;
        return false;
    }
    if (!good) good = book_auto_cover_render(path, title, author, BOOK_COVER_W, BOOK_COVER_H, out);
    if (good && cache_result) cover_cache_write(cache_path, &key, out);
    return good;
}

// 与网格缓存分离，不能把已经裁剪的封面当作完整原图。
// Separate from grid thumbnails: a cropped cache cannot represent the complete source.
static bool book_cover_load_list_gray_impl(const char *path, const char *title, const char *author,
                               uint8_t out[BOOK_COVER_W * BOOK_COVER_H], bool allow_decode,
                               bool *pending, unsigned *width, unsigned *height, bool allow_auto, bool *auto_cache) {
    if (pending) *pending = false;
    if (!path || !out || !width || !height) return false;
    *width = BOOK_COVER_W; *height = BOOK_COVER_H;
    char canonical[256], cache_path[120];
    if (book_title_from_path(path, canonical, sizeof(canonical))) title = canonical;
    if (!title || !*title) return false;
    cover_cache_header_t key;
    if (!cover_cache_key(path, title, author, cache_path, sizeof(cache_path), &key)) return false;
    char *suffix = strrchr(cache_path, '.');
    if (!suffix || (size_t)(suffix-cache_path)+10 >= sizeof(cache_path)) return false;
    strcpy(suffix, "-list.bin");
    FILE *file = fopen(cache_path, "rb");
    if (file) {
        cover_cache_header_t found;
        bool good = fread(&found, 1, sizeof(found), file) == sizeof(found) &&
            found.magic == key.magic && found.schema == key.schema &&
            found.template_version == key.template_version && found.file_size == key.file_size &&
            found.modified == key.modified && found.path_hash == key.path_hash && found.text_hash == key.text_hash &&
            found.width && found.height && found.width <= BOOK_COVER_W && found.height <= BOOK_COVER_H;
        size_t bytes = good ? (size_t)found.width * found.height : 0;
        if (good) good = fread(out, 1, bytes, file) == bytes && fgetc(file) == EOF;
        fclose(file);
        if (good) { *width = found.width; *height = found.height; return true; }
    }
    if (!allow_decode) { if (pending) *pending = true; return false; }
    bool good = false, cacheable = true;
    if (book_metadata_supported(path)) {
        uint8_t *data = NULL; size_t size = 0; bool png = false;
        size_t available = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        size_t budget = available > 1536u*1024u ? available - 1536u*1024u : 0;
        if (budget > 4u*1024u*1024u) budget = 4u*1024u*1024u;
        esp_err_t error = budget ? book_metadata_cover_bounded(path, &data, &size, &png, budget) : ESP_ERR_NO_MEM;
        unsigned sw = 0, sh = 0;
        if (error == ESP_OK && book_image_dimensions(data, size, png, &sw, &sh) && sw && sh) {
            if ((uint64_t)sw*BOOK_COVER_H > (uint64_t)sh*BOOK_COVER_W) {
                *width = BOOK_COVER_W; *height = ((uint64_t)sh*BOOK_COVER_W + sw/2)/sw;
            } else { *height = BOOK_COVER_H; *width = ((uint64_t)sw*BOOK_COVER_H + sh/2)/sh; }
            if (!*width) *width = 1;
            if (!*height) *height = 1;
            good = book_image_grayscale(data, size, png, *width, *height, out);
        }
        free(data);
        if ((error != ESP_OK && error != ESP_ERR_NOT_FOUND) || (error == ESP_OK && !good)) cacheable = false;
    }
    if (!good && !allow_auto) { if(pending)*pending=true;if(auto_cache)*auto_cache=cacheable;return false; }
    if (!good) {
        *width = BOOK_COVER_W; *height = BOOK_COVER_H;
        good = book_auto_cover_render(path, title, author, *width, *height, out);
    }
    if (good && cacheable) {
        // 有效尺寸和像素一起原子落盘；最多一张小图，无整页解码工作区。
        // Persist dimensions and pixels atomically; one small image, never a whole-page decode workspace.
        key.width = *width; key.height = *height;
        char temp[128];
        if ((!mkdir("/sdcard/.readpico",0777) || errno==EEXIST) &&
            (!mkdir("/sdcard/.readpico/covers",0777) || errno==EEXIST) &&
            snprintf(temp,sizeof(temp),"%s.tmp",cache_path)<(int)sizeof(temp)) {
            file = fopen(temp,"wb");
            if (file) {
                size_t bytes = (size_t)*width * *height;
                bool saved = fwrite(&key,1,sizeof(key),file)==sizeof(key) && fwrite(out,1,bytes,file)==bytes;
                if (fclose(file)) saved = false;
                if (!saved || rename(temp,cache_path)) (void)remove(temp);
            }
        }
    }
    return good;
}

/* ---- 已处理的黑白点阵落卡 / Persistent finished monochrome raster ---- */
// 更改封面点阵算法时递增此版本；首页与书架各保留一个快刷结果，不按相位累积文件。
// Bump for changes to dithering; retain one Home and one shelf raster per book, not one per phase.
#define FAST_COVER_REVISION 1u
typedef struct {
    cover_cache_header_t source;
    uint32_t revision, width, height, stride, contrast, phase, white_edge;
    uint64_t checksum;
} fast_cover_header_t;
static uint64_t fast_cover_checksum(const uint8_t *data, size_t size) {
    uint64_t hash = UINT64_C(14695981039346656037);
    while (size--) hash = (hash ^ *data++) * UINT64_C(1099511628211);
    return hash;
}
static bool fast_cover_key(const char *source, const char *title, const char *author,
                            book_fast_cover_key_t key, char path[120], fast_cover_header_t *header) {
    const bool home = key.width == 172 && key.height == 250;
    if (!source || !*source || !key.width || key.width > 176 || !key.height ||
        (key.height > 240 && !home) ||
        key.stride < (key.width+7)/8 || key.stride > 22 || key.phase > 15 || key.contrast > 200) return false;
    char canonical[256];
    if (book_title_from_path(source, canonical, sizeof(canonical))) title = canonical;
    if (!title || !*title) return false;
    memset(header, 0, sizeof(*header));
    if (!cover_cache_key(source, title, author, path, 120, &header->source)) return false;
    snprintf(path, 120, "/sdcard/.readpico/covers/%016llx-%sfast.bin",
             (unsigned long long)header->source.path_hash, home ? "home-" : "");
    header->revision = FAST_COVER_REVISION; header->width = key.width; header->height = key.height;
    header->stride = key.stride; header->contrast = key.contrast; header->phase = key.phase;
    return true;
}
bool book_cover_fast_cache_read(const char *source, const char *title, const char *author,
                                book_fast_cover_key_t key, uint8_t *out, bool *white_edge) {
    char path[120]; fast_cover_header_t expected, found;
    if (!out || !white_edge || !fast_cover_key(source, title, author, key, path, &expected)) return false;
    FILE *file = fopen(path, "rb"); if (!file) return false;
    const size_t bytes = (size_t)key.stride * key.height;
    bool ok = fread(&found, 1, sizeof(found), file) == sizeof(found) &&
        found.source.magic == expected.source.magic && found.source.schema == expected.source.schema &&
        found.source.template_version == expected.source.template_version &&
        found.source.file_size == expected.source.file_size && found.source.modified == expected.source.modified &&
        found.source.path_hash == expected.source.path_hash && found.source.text_hash == expected.source.text_hash &&
        found.revision == expected.revision && found.width == key.width && found.height == key.height &&
        found.stride == key.stride && found.contrast == key.contrast && found.phase == key.phase &&
        found.white_edge <= 1 && fread(out, 1, bytes, file) == bytes && fgetc(file) == EOF &&
        fast_cover_checksum(out, bytes) == found.checksum;
    fclose(file);
    if (ok) *white_edge = found.white_edge != 0;
    return ok;
}
static void fast_cache_write(const char *source,const char *title,const char *author,
                             book_fast_cover_key_t key,const uint8_t *out,bool white_edge,int64_t size,int64_t modified) {
    char path[120], temp[128]; fast_cover_header_t header;
    if (!out || !fast_cover_key(source, title, author, key, path, &header)) return;
    if(size>=0&&(header.source.file_size!=(uint64_t)size||header.source.modified!=modified))return;
    if ((mkdir("/sdcard/.readpico", 0777) && errno != EEXIST) ||
        (mkdir("/sdcard/.readpico/covers", 0777) && errno != EEXIST)) return;
    if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int)sizeof(temp)) return;
    header.white_edge = white_edge;
    const size_t bytes = (size_t)key.stride * key.height;
    header.checksum = fast_cover_checksum(out, bytes);
    FILE *file = fopen(temp, "wb"); if (!file) return;
    bool ok = fwrite(&header, 1, sizeof(header), file) == sizeof(header) && fwrite(out, 1, bytes, file) == bytes;
    if (fclose(file)) ok = false;
    if (ok && !rename(temp, path)) return;
    (void)remove(temp);
}

bool book_cover_load_gray(const char *p,const char *t,const char *a,uint8_t out[BOOK_COVER_W * BOOK_COVER_H],bool decode,bool *pending) {
    return book_cover_load_gray_impl(p,t,a,out,decode,pending,true,NULL);
}
bool book_cover_load_list_gray(const char *p,const char *t,const char *a,uint8_t out[BOOK_COVER_W * BOOK_COVER_H],bool decode,bool *pending,unsigned *w,unsigned *h) {
    return book_cover_load_list_gray_impl(p,t,a,out,decode,pending,w,h,true,NULL);
}
int book_cover_load_io(const char *p,const char *t,const char *a,uint8_t *out,bool list,unsigned *w,unsigned *h,bool *auto_cache) {
    bool automatic=false;*w=BOOK_COVER_W;*h=BOOK_COVER_H;
    bool ready=list?book_cover_load_list_gray_impl(p,t,a,out,true,&automatic,w,h,false,auto_cache):
                    book_cover_load_gray_impl(p,t,a,out,true,&automatic,false,auto_cache);
    return ready?1:automatic?0:-1;
}
bool book_cover_list_cache_dimensions(const char *p,const char *t,const char *a,unsigned *w,unsigned *h) {
    char canonical[256],path[120];cover_cache_header_t key,found;
    if(book_title_from_path(p,canonical,sizeof(canonical)))t=canonical;
    if(!cover_cache_key(p,t,a,path,sizeof(path),&key))return false;
    char *suffix=strrchr(path,'.');if(!suffix)return false;strcpy(suffix,"-list.bin");
    FILE *f=fopen(path,"rb");if(!f)return false;
    bool ok=fread(&found,1,sizeof(found),f)==sizeof(found)&&found.magic==key.magic&&found.schema==key.schema&&
        found.template_version==key.template_version&&found.file_size==key.file_size&&found.modified==key.modified&&
        found.path_hash==key.path_hash&&found.text_hash==key.text_hash&&found.width&&found.height&&
        found.width<=BOOK_COVER_W&&found.height<=BOOK_COVER_H;
    fclose(f);if(ok){*w=found.width;*h=found.height;}return ok;
}
void book_cover_store_generated(const char *p,const char *t,const char *a,const uint8_t *pixels,bool list,int64_t size,int64_t modified) {
    char canonical[256],path[120];cover_cache_header_t key;
    if(book_title_from_path(p,canonical,sizeof(canonical)))t=canonical;
    if(!cover_cache_key(p,t,a,path,sizeof(path),&key))return;
    if(key.file_size!=(uint64_t)size||key.modified!=modified)return;
    if(list){char *suffix=strrchr(path,'.');if(!suffix)return;strcpy(suffix,"-list.bin");}
    cover_cache_write(path,&key,pixels);
}

void book_cover_fast_cache_write(const char *p,const char *t,const char *a,book_fast_cover_key_t key,const uint8_t *pixels,bool edge) {
    fast_cache_write(p,t,a,key,pixels,edge,-1,0);
}
void book_cover_fast_cache_write_checked(const char *p,const char *t,const char *a,book_fast_cover_key_t key,const uint8_t *pixels,bool edge,int64_t size,int64_t modified) {
    fast_cache_write(p,t,a,key,pixels,edge,size,modified);
}

// 与壁纸解码共用行窗口，不保留整张源RGBA图。/ Reuse wallpaper row windows without retaining full-source RGBA.
