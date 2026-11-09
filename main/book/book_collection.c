/* 中文：书架逻辑分类的事务、一次迁移与版本化双副本；不调用文件移动或阅读记录接口。
 * English: Logical shelf transactions, one-time migration and versioned dual copies; no file-move or reading-record calls. */
#include "book_collection.h"
#include "book_shelf_categories.h"
#include "esp_heap_caps.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { uint32_t category; char path[BOOK_STORE_PATH_MAX]; } collection_book_t;
struct book_collection_data {
    book_collection_category_t categories[BOOK_COLLECTION_CATEGORIES];
    int count;
    uint32_t next_id;
    bool migrated;
    collection_book_t *books;
    size_t used, capacity;
    uint16_t lookup[BOOK_COLLECTION_BOOKS * 2];
};

#define COLLECTION_HEADER 32
#define COLLECTION_LIMIT (COLLECTION_HEADER + BOOK_COLLECTION_CATEGORIES * 396u + BOOK_COLLECTION_BOOKS * 296u)
static const uint8_t collection_magic[8] = {'M','G','S','H','E','L','F',2};

static void *allocate(size_t n) { return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
static void release(book_collection_data_t *d) { if (d) { free(d->books); free(d); } }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void put32(uint8_t *p, uint32_t n) { for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(8*i)); }
static uint32_t checksum(const void *data, size_t len) {
    uint32_t crc=UINT32_MAX; const uint8_t *p=data;
    for(size_t i=0;i<len;++i) { crc^=p[i]; for(unsigned b=0;b<8;++b) crc=(crc>>1)^(UINT32_C(0xedb88320)&(0u-(crc&1u))); }
    return ~crc;
}

bool book_collection_name(const char *input, char out[BOOK_COLLECTION_NAME_BYTES]) {
    if(!input || !out)return false;
    while(*input==' ' || *input=='\t')++input;
    size_t n=strnlen(input,BOOK_COLLECTION_NAME_BYTES+8);
    while(n && (input[n-1]==' ' || input[n-1]=='\t'))--n;
    if(!n || n>=BOOK_COLLECTION_NAME_BYTES)return false;
    unsigned chars=0;
    for(size_t i=0;i<n;) {
        unsigned char c=(unsigned char)input[i]; unsigned bytes=1; uint32_t cp=c;
        if(c>=0xc2 && c<=0xdf){bytes=2;cp=c&31;}
        else if(c>=0xe0 && c<=0xef){bytes=3;cp=c&15;}
        else if(c>=0xf0 && c<=0xf4){bytes=4;cp=c&7;}
        else if(c>=128)return false;
        if(i+bytes>n)return false;
        for(unsigned j=1;j<bytes;++j){unsigned char next=(unsigned char)input[i+j];if((next&0xc0)!=0x80)return false;cp=(cp<<6)|(next&63);}
        if(cp<32 || cp==127 || cp=='/' || cp=='\\' || cp==0x2028 || cp==0x2029 ||
           (bytes==2 && cp<0x80) || (bytes==3 && cp<0x800) || (bytes==4 && cp<0x10000) ||
           (cp>=0xd800 && cp<=0xdfff) || cp>0x10ffff || ++chars>BOOK_COLLECTION_NAME_CHARS)return false;
        i+=bytes;
    }
    memcpy(out,input,n);out[n]=0;
    return strcmp(out,"全部书籍") && strcmp(out,"未分类") && strcmp(out,".") && strcmp(out,"..");
}

static int category_index(const book_collection_data_t *d,uint32_t id) {
    if(d)for(int i=0;i<d->count;++i)if(d->categories[i].id==id)return i;
    return -1;
}
static int record_index(const book_collection_data_t *d,const char *path) {
    if(!d || !path)return -1;
    uint32_t hash=UINT32_C(2166136261);
    for(const unsigned char *p=(const unsigned char *)path;*p;++p)hash=(hash^*p)*UINT32_C(16777619);
    size_t slot=hash%(BOOK_COLLECTION_BOOKS*2);
    for(size_t n=0;n<BOOK_COLLECTION_BOOKS*2;++n){
        uint16_t value=d->lookup[slot];if(!value)return -1;
        if(!strcmp(d->books[value-1].path,path))return value-1;
        slot=(slot+1)%(BOOK_COLLECTION_BOOKS*2);
    }
    return -1;
}
static bool valid_path(const char *path) {
    return path && path[0]=='/' && strnlen(path,BOOK_STORE_PATH_MAX)<BOOK_STORE_PATH_MAX &&
           !strstr(path,"/../") && !strstr(path,"/./") && !strstr(path,"//") &&
           strcmp(path,"/..") && strcmp(path,"/.") &&
           (strlen(path)<3 || strcmp(path+strlen(path)-3,"/..")) &&
           (strlen(path)<2 || strcmp(path+strlen(path)-2,"/."));
}
static bool reserve(book_collection_data_t *d,size_t needed) {
    if(needed<=d->capacity)return true;
    if(needed>BOOK_COLLECTION_BOOKS)return false;
    size_t capacity=d->capacity?d->capacity*2:32;if(capacity<needed)capacity=needed;
    if(capacity>BOOK_COLLECTION_BOOKS)capacity=BOOK_COLLECTION_BOOKS;
    collection_book_t *next=heap_caps_realloc(d->books,capacity*sizeof(*next),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!next)return false;
    d->books=next;d->capacity=capacity;return true;
}
static book_collection_data_t *clone(const book_collection_data_t *d) {
    book_collection_data_t *next=allocate(sizeof(*next));if(!next)return NULL;
    if(d){*next=*d;next->books=NULL;next->capacity=0;
        if(!reserve(next,d->used)){free(next);return NULL;}
        if(d->used)memcpy(next->books,d->books,d->used*sizeof(*d->books));
    }else next->next_id=1;
    return next;
}
static esp_err_t add_category(book_collection_data_t *d,const char *name,uint32_t *id,bool reuse) {
    char clean[BOOK_COLLECTION_NAME_BYTES];if(!book_collection_name(name,clean))return ESP_ERR_INVALID_ARG;
    for(int i=0;i<d->count;++i)if(!strcmp(d->categories[i].name,clean)){
        if(!reuse)return ESP_ERR_INVALID_STATE;
        *id=d->categories[i].id;return ESP_OK;
    }
    if(d->count==BOOK_COLLECTION_CATEGORIES || !d->next_id || d->next_id==BOOK_COLLECTION_ALL)return ESP_ERR_INVALID_SIZE;
    book_collection_category_t *category=&d->categories[d->count++];
    memset(category,0,sizeof(*category));
    category->id=d->next_id++;memcpy(category->name,clean,strlen(clean)+1);*id=category->id;return ESP_OK;
}
static esp_err_t set_record(book_collection_data_t *d,const char *path,uint32_t id) {
    if(!valid_path(path))return ESP_ERR_INVALID_ARG;
    int i=record_index(d,path);
    if(i<0){if(d->used==BOOK_COLLECTION_BOOKS)return ESP_ERR_INVALID_SIZE;
        if(!reserve(d,d->used+1))return ESP_ERR_NO_MEM;
        i=(int)d->used++;
        memset(&d->books[i],0,sizeof(d->books[i]));memcpy(d->books[i].path,path,strlen(path)+1);
        uint32_t hash=UINT32_C(2166136261);
        for(const unsigned char *p=(const unsigned char *)path;*p;++p)hash=(hash^*p)*UINT32_C(16777619);
        size_t slot=hash%(BOOK_COLLECTION_BOOKS*2);
        while(d->lookup[slot])slot=(slot+1)%(BOOK_COLLECTION_BOOKS*2);
        d->lookup[slot]=(uint16_t)(i+1);
    }
    d->books[i].category=id;return ESP_OK;
}

static uint8_t *encode(const book_collection_data_t *d,uint32_t generation,size_t *size) {
    size_t n=COLLECTION_HEADER;
    for(int i=0;i<d->count;++i)n+=12+strlen(d->categories[i].name)+strlen(d->categories[i].legacy_name);
    for(size_t i=0;i<d->used;++i)n+=8+strlen(d->books[i].path);
    uint8_t *bytes=allocate(n);if(!bytes)return NULL;
    memcpy(bytes,collection_magic,8);put32(bytes+8,generation);put32(bytes+12,(uint32_t)n);
    put32(bytes+20,d->next_id);bytes[24]=(uint8_t)d->count;bytes[25]=d->migrated;
    put32(bytes+28,(uint32_t)d->used);size_t at=COLLECTION_HEADER;
    for(int i=0;i<d->count;++i){size_t len=strlen(d->categories[i].name);put32(bytes+at,d->categories[i].id);
        size_t legacy_len=strlen(d->categories[i].legacy_name);
        put32(bytes+at+4,(uint32_t)len);put32(bytes+at+8,(uint32_t)legacy_len);
        memcpy(bytes+at+12,d->categories[i].name,len);memcpy(bytes+at+12+len,d->categories[i].legacy_name,legacy_len);at+=12+len+legacy_len;}
    for(size_t i=0;i<d->used;++i){size_t len=strlen(d->books[i].path);put32(bytes+at,d->books[i].category);
        put32(bytes+at+4,(uint32_t)len);memcpy(bytes+at+8,d->books[i].path,len);at+=8+len;}
    put32(bytes+16,checksum(bytes,n));*size=n;return bytes;
}
static esp_err_t decode(uint8_t *bytes,size_t size,book_collection_data_t **out,uint32_t *generation) {
    *out=NULL;
    if(size<COLLECTION_HEADER || size>COLLECTION_LIMIT || memcmp(bytes,collection_magic,8) ||
       get32(bytes+12)!=size || bytes[24]>BOOK_COLLECTION_CATEGORIES || bytes[25]>1 || bytes[26] || bytes[27] ||
       get32(bytes+28)>BOOK_COLLECTION_BOOKS)return ESP_ERR_INVALID_RESPONSE;
    uint32_t stored=get32(bytes+16);put32(bytes+16,0);uint32_t actual=checksum(bytes,size);put32(bytes+16,stored);
    if(stored!=actual || !get32(bytes+8))return ESP_ERR_INVALID_RESPONSE;
    book_collection_data_t *d=clone(NULL);if(!d)return ESP_ERR_NO_MEM;
    d->next_id=get32(bytes+20);d->migrated=bytes[25];
    size_t at=COLLECTION_HEADER;esp_err_t err=ESP_OK;
    for(unsigned i=0;i<bytes[24] && err==ESP_OK;++i){
        if(size-at<12){err=ESP_ERR_INVALID_RESPONSE;break;}
        uint32_t id=get32(bytes+at),len=get32(bytes+at+4),legacy_len=get32(bytes+at+8);at+=12;
        if(!id || id==BOOK_COLLECTION_ALL || id>=d->next_id || category_index(d,id)>=0 || !len ||
           len>=BOOK_COLLECTION_NAME_BYTES || legacy_len>=BOOK_STORE_PATH_MAX || len+legacy_len>size-at){err=ESP_ERR_INVALID_RESPONSE;break;}
        char name[BOOK_COLLECTION_NAME_BYTES]={0},clean[BOOK_COLLECTION_NAME_BYTES];memcpy(name,bytes+at,len);at+=len;
        if(strlen(name)!=len || !book_collection_name(name,clean) || strcmp(name,clean)){err=ESP_ERR_INVALID_RESPONSE;break;}
        for(int j=0;j<d->count;++j)if(!strcmp(d->categories[j].name,name))err=ESP_ERR_INVALID_RESPONSE;
        book_collection_category_t *category=&d->categories[d->count];
        memcpy(category->legacy_name,bytes+at,legacy_len);at+=legacy_len;
        if(strlen(category->legacy_name)!=legacy_len || strchr(category->legacy_name,'/')){err=ESP_ERR_INVALID_RESPONSE;break;}
        for(int j=0;legacy_len && j<d->count;++j)if(!strcmp(d->categories[j].legacy_name,category->legacy_name))err=ESP_ERR_INVALID_RESPONSE;
        category->id=id;memcpy(category->name,name,len+1);++d->count;
    }
    uint32_t records=get32(bytes+28);
    if(err==ESP_OK && !reserve(d,records))err=ESP_ERR_NO_MEM;
    for(uint32_t i=0;i<records && err==ESP_OK;++i){
        if(size-at<8){err=ESP_ERR_INVALID_RESPONSE;break;}
        uint32_t id=get32(bytes+at),len=get32(bytes+at+4);at+=8;
        if((id && category_index(d,id)<0) || !len || len>=BOOK_STORE_PATH_MAX || len>size-at){err=ESP_ERR_INVALID_RESPONSE;break;}
        char path[BOOK_STORE_PATH_MAX]={0};memcpy(path,bytes+at,len);at+=len;
        if(strlen(path)!=len || !valid_path(path) || record_index(d,path)>=0){err=ESP_ERR_INVALID_RESPONSE;break;}
        err=set_record(d,path,id);
    }
    if(at!=size || !d->next_id || d->next_id==BOOK_COLLECTION_ALL)err=ESP_ERR_INVALID_RESPONSE;
    if(err!=ESP_OK){release(d);return err;}*generation=get32(bytes+8);*out=d;return ESP_OK;
}
static esp_err_t read_image(FILE *f,book_collection_data_t **d,uint32_t *generation) {
    *d=NULL;
    uint8_t header[COLLECTION_HEADER];
    if(fread(header,1,sizeof(header),f)!=sizeof(header))return ferror(f)?ESP_FAIL:ESP_ERR_INVALID_RESPONSE;
    uint32_t size=get32(header+12);if(size<COLLECTION_HEADER || size>COLLECTION_LIMIT)return ESP_ERR_INVALID_RESPONSE;
    uint8_t *bytes=allocate(size);if(!bytes)return ESP_ERR_NO_MEM;memcpy(bytes,header,sizeof(header));
    esp_err_t err=ESP_ERR_INVALID_RESPONSE;
    if(fread(bytes+sizeof(header),1,size-sizeof(header),f)==size-sizeof(header))err=decode(bytes,size,d,generation);
    else if(ferror(f))err=ESP_FAIL;
    free(bytes);return err;
}
static esp_err_t read_slot(const char *path,book_collection_data_t **d,uint32_t *generation) {
    FILE *f=fopen(path,"rb");if(!f)return errno==ENOENT?ESP_ERR_NOT_FOUND:ESP_FAIL;
    esp_err_t err=read_image(f,d,generation);
    if(err==ESP_OK){int extra=fgetc(f);if(ferror(f) || extra!=EOF){release(*d);*d=NULL;err=ferror(f)?ESP_FAIL:ESP_ERR_INVALID_RESPONSE;}}
    if(fclose(f)!=0 && err==ESP_OK){release(*d);*d=NULL;err=ESP_FAIL;}return err;
}
static bool slot_path(const book_collection_t *s,int slot,char path[BOOK_STORE_PATH_MAX]) {
    size_t n=strnlen(s->prefix,BOOK_STORE_PATH_MAX);
    if(n>=BOOK_STORE_PATH_MAX-2 || (slot!=0 && slot!=1))return false;
    memcpy(path,s->prefix,n);path[n]='.';path[n+1]=(char)('0'+slot);path[n+2]=0;
    return true;
}
esp_err_t book_collection_open(book_collection_t *s,const char *prefix) {
    if(!s || !valid_path(prefix) || strlen(prefix)+3>=BOOK_STORE_PATH_MAX)return ESP_ERR_INVALID_ARG;
    book_collection_close(s);memcpy(s->prefix,prefix,strlen(prefix)+1);s->slot=-1;
    esp_err_t errors[2];uint32_t generations[2]={0};book_collection_data_t *data[2]={0};
    for(int i=0;i<2;++i){char path[BOOK_STORE_PATH_MAX];slot_path(s,i,path);errors[i]=read_slot(path,&data[i],&generations[i]);}
    // 不能把暂时读不出的较新副本当成损坏并覆盖。/ Never overwrite a potentially newer copy after a transient read/allocation failure.
    if(errors[0]==ESP_ERR_NO_MEM || errors[1]==ESP_ERR_NO_MEM || errors[0]==ESP_FAIL || errors[1]==ESP_FAIL){
        release(data[0]);release(data[1]);s->blocked=true;
        return errors[0]==ESP_ERR_NO_MEM || errors[1]==ESP_ERR_NO_MEM?ESP_ERR_NO_MEM:ESP_FAIL;
    }
    int selected=errors[1]==ESP_OK && (errors[0]!=ESP_OK || generations[1]>generations[0])?1:0;
    if(errors[selected]==ESP_OK){s->data=data[selected];release(data[1-selected]);s->slot=selected;
        s->generation=generations[selected];s->ready=true;
        s->recovered=errors[1-selected]!=ESP_OK && errors[1-selected]!=ESP_ERR_NOT_FOUND;return ESP_OK;}
    release(data[0]);release(data[1]);
    if(errors[0]==ESP_ERR_NOT_FOUND && errors[1]==ESP_ERR_NOT_FOUND){s->data=clone(NULL);s->ready=s->data!=NULL;return s->ready?ESP_OK:ESP_ERR_NO_MEM;}
    s->blocked=true;return errors[0]==ESP_ERR_NO_MEM || errors[1]==ESP_ERR_NO_MEM?ESP_ERR_NO_MEM:ESP_ERR_INVALID_RESPONSE;
}
void book_collection_drop_undo(book_collection_t *s){if(s){release(s->undo);s->undo=NULL;}}
void book_collection_close(book_collection_t *s){if(!s)return;release(s->data);release(s->undo);memset(s,0,sizeof(*s));s->slot=-1;}
bool book_collection_can_undo(const book_collection_t *s){return s && s->undo && s->ready && !s->blocked;}
bool book_collection_migrated(const book_collection_t *s){return s && s->data && s->data->migrated;}
int book_collection_count(const book_collection_t *s){return s && s->data?s->data->count:0;}
const book_collection_category_t *book_collection_at(const book_collection_t *s,int i){return s && s->data && i>=0 && i<s->data->count?&s->data->categories[i]:NULL;}
const book_collection_category_t *book_collection_find(const book_collection_t *s,uint32_t id){return book_collection_at(s,s?category_index(s->data,id):-1);}
uint32_t book_collection_legacy_id(const book_collection_t *s,const char *name){
    if(s && s->data && name && *name)for(int i=0;i<s->data->count;++i)
        if(!strcmp(s->data->categories[i].legacy_name,name))return s->data->categories[i].id;
    return 0;
}
uint32_t book_collection_assignment(const book_collection_t *s,const char *path){int i=s?record_index(s->data,path):-1;return i>=0?s->data->books[i].category:0;}

static esp_err_t commit(book_collection_t *s,book_collection_data_t *next,bool undo) {
    if(!s->ready || s->blocked || s->generation==UINT32_MAX){release(next);return ESP_ERR_INVALID_STATE;}
    size_t size=0;uint8_t *bytes=encode(next,s->generation+1,&size);
    if(!bytes){release(next);return ESP_ERR_NO_MEM;}
    char path[BOOK_STORE_PATH_MAX];int slot=s->slot==0?1:0;slot_path(s,slot,path);
    FILE *file=fopen(path,"wb");esp_err_t err=ESP_FAIL;
    if(file){bool ok=fwrite(bytes,1,size,file)==size && fflush(file)==0 && fsync(fileno(file))==0;
        if(fclose(file)!=0)ok=false;
        err=ok?ESP_OK:ESP_FAIL;}
    if(err==ESP_OK){book_collection_data_t *check=NULL;uint32_t generation=0;
        err=read_slot(path,&check,&generation);
        if(err==ESP_OK && generation!=s->generation+1)err=ESP_ERR_INVALID_RESPONSE;
        release(check);}
    free(bytes);
    if(err!=ESP_OK){
        // 未校验的新副本不能在重启时胜过旧副本；无法清理时阻断继续写入。
        // An unverified candidate must not outrank the old copy after reboot; block further writes if cleanup fails.
        if(unlink(path)!=0 && errno!=ENOENT)s->blocked=true;
        release(next);return err;
    }
    book_collection_drop_undo(s);
    if(undo)s->undo=s->data;else release(s->data);
    s->data=next;s->slot=slot;++s->generation;s->recovered=false;return ESP_OK;
}
void book_collection_apply(const book_collection_t *s,book_shelf_entry_t *books,int count){
    for(int i=0;i<count;++i)books[i].category_id=book_collection_assignment(s,books[i].path);
}
static esp_err_t legacy_source(book_collection_data_t *d,const char *name) {
    if(!name || !*name || strlen(name)>=BOOK_STORE_PATH_MAX || strchr(name,'/'))return ESP_ERR_INVALID_ARG;
    for(int i=0;i<d->count;++i)if(!strcmp(d->categories[i].legacy_name,name))return ESP_OK;
    if(d->count==BOOK_COLLECTION_CATEGORIES || !d->next_id || d->next_id==BOOK_COLLECTION_ALL)return ESP_ERR_INVALID_SIZE;
    book_collection_category_t *category=&d->categories[d->count++];
    memset(category,0,sizeof(*category));category->id=d->next_id++;
    memcpy(category->legacy_name,name,strlen(name)+1);return ESP_OK;
}
static void legacy_names(book_collection_data_t *d) {
    // 先保留所有合法原名，再分配有界别名，避免与尚未处理的原名冲突。
    // Reserve every valid original name before assigning bounded aliases, including collisions with later originals.
    for(int i=0;i<d->count;++i){char clean[BOOK_COLLECTION_NAME_BYTES];
        if(book_collection_name(d->categories[i].legacy_name,clean) && !strcmp(clean,d->categories[i].legacy_name))
            memcpy(d->categories[i].name,clean,strlen(clean)+1);
    }
    for(int i=0;i<d->count;++i){
        if(d->categories[i].name[0])continue;
        const char *raw=d->categories[i].legacy_name;char prefix[BOOK_COLLECTION_NAME_BYTES]={0},clean[BOOK_COLLECTION_NAME_BYTES];
        size_t len=0;unsigned chars=0;
        while(raw[len] && chars<18){unsigned char c=(unsigned char)raw[len];size_t bytes=c<128?1:c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
            if(!bytes || len+bytes>strlen(raw))break;
            memcpy(prefix+len,raw+len,bytes);prefix[len+bytes]=0;
            if(!book_collection_name(prefix,clean)) {prefix[len]=0;break;}
            len+=bytes;++chars;
        }
        if(!book_collection_name(prefix,clean))memcpy(clean,"原目录",sizeof("原目录"));
        for(unsigned suffix=1;;++suffix){char alias[BOOK_COLLECTION_NAME_BYTES];
            snprintf(alias,sizeof(alias),"%.72s %u",clean,suffix);bool used=false;
            for(int j=0;j<d->count;++j)if(!strcmp(d->categories[j].name,alias)){used=true;break;}
            if(!used){memcpy(d->categories[i].name,alias,strlen(alias)+1);break;}
        }
    }
}
esp_err_t book_collection_sync(book_collection_t *s,const char *root,const char *const *folders,size_t folder_count,
                               book_shelf_entry_t *books,int count,bool complete) {
    if(!s || !s->ready || s->blocked)return ESP_ERR_INVALID_STATE;
    if(!root || count<0 || (count && !books) || (folder_count && !folders))return ESP_ERR_INVALID_ARG;
    book_collection_apply(s,books,count);
    if(!complete)return ESP_OK;
    bool migrate=!s->data->migrated,changed=migrate;
    for(int i=0;i<count;++i){books[i].new_book=!migrate && !books[i].removed && record_index(s->data,books[i].path)<0;if(books[i].new_book)changed=true;}
    if(!changed)return ESP_OK;
    book_collection_data_t *next=clone(s->data);if(!next)return ESP_ERR_NO_MEM;esp_err_t err=ESP_OK;
    if(migrate)for(size_t i=0;i<folder_count && err==ESP_OK;++i){
        if(!valid_path(folders[i])){err=ESP_ERR_INVALID_ARG;break;}
        const char *base=strrchr(folders[i],'/');err=legacy_source(next,base?base+1:folders[i]);}
    if(migrate && err==ESP_OK){
        for(int i=0;i<count && err==ESP_OK;++i){char child[BOOK_STORE_PATH_MAX];
            if(!books[i].removed && book_shelf_child(root,books[i].path,child,sizeof(child)))err=legacy_source(next,child);
        }
        if(err==ESP_OK)legacy_names(next);
    }
    for(int i=0;i<count && err==ESP_OK;++i){
        if(books[i].removed || record_index(next,books[i].path)>=0)continue;
        uint32_t id=0;char child[BOOK_STORE_PATH_MAX];
        if(migrate && book_shelf_child(root,books[i].path,child,sizeof(child)))
            for(int j=0;j<next->count;++j)if(!strcmp(next->categories[j].legacy_name,child)){id=next->categories[j].id;break;}
        if(err==ESP_OK)err=set_record(next,books[i].path,id);
    }
    if(err!=ESP_OK){release(next);return err;}next->migrated=true;
    err=commit(s,next,false);if(err==ESP_OK)book_collection_apply(s,books,count);return err;
}
static esp_err_t assign_to(book_collection_data_t *d,const book_shelf_entry_t *books,int count,bool selected,uint32_t id,unsigned *changed){
    *changed=0;if(count<0 || (count && !books) || (id && category_index(d,id)<0))return ESP_ERR_INVALID_ARG;
    for(int i=0;i<count;++i){if(books[i].removed || (selected && !books[i].selected))continue;
        int index=record_index(d,books[i].path);uint32_t old=index>=0?d->books[index].category:0;
        if(old==id)continue;
        esp_err_t err=set_record(d,books[i].path,id);if(err!=ESP_OK)return err;++*changed;}
    return ESP_OK;
}
esp_err_t book_collection_create(book_collection_t *s,const char *name,const book_shelf_entry_t *books,int count,bool selected,uint32_t *id,unsigned *changed){
    if(!s || !s->ready || s->blocked || !id || !changed)return ESP_ERR_INVALID_STATE;
    *changed=0;
    book_collection_data_t *next=clone(s->data);if(!next)return ESP_ERR_NO_MEM;uint32_t created=0;
    esp_err_t err=add_category(next,name,&created,false);
    unsigned done=0;if(err==ESP_OK)err=assign_to(next,books,count,selected,created,&done);
    if(err!=ESP_OK){release(next);return err;}err=commit(s,next,true);if(err==ESP_OK){*id=created;*changed=done;}return err;
}
esp_err_t book_collection_assign(book_collection_t *s,const book_shelf_entry_t *books,int count,bool selected,uint32_t target,unsigned *changed){
    if(!s || !s->ready || s->blocked || !changed)return ESP_ERR_INVALID_STATE;
    *changed=0;
    book_collection_data_t *next=clone(s->data);if(!next)return ESP_ERR_NO_MEM;unsigned done=0;
    esp_err_t err=assign_to(next,books,count,selected,target,&done);
    if(err!=ESP_OK || !done){release(next);return err;}err=commit(s,next,true);if(err==ESP_OK)*changed=done;return err;
}
esp_err_t book_collection_unclassify(book_collection_t *s, const book_shelf_entry_t *books,
                                     int count, uint32_t source, unsigned *changed) {
    if (!s || !s->ready || s->blocked || !changed) return ESP_ERR_INVALID_STATE;
    *changed = 0;
    if (count < 0 || (count && !books) || category_index(s->data, source) < 0) return ESP_ERR_INVALID_ARG;
    book_collection_data_t *next = clone(s->data);
    if (!next) return ESP_ERR_NO_MEM;
    unsigned done = 0;
    for (int i = 0; i < count; ++i) {
        if (!books[i].selected || books[i].removed) continue;
        int index = record_index(next, books[i].path);
        if (index >= 0 && next->books[index].category == source) {
            next->books[index].category = 0;
            ++done;
        }
    }
    if (!done) { release(next); return ESP_OK; }
    esp_err_t err = commit(s, next, true);
    if (err == ESP_OK) *changed = done;
    return err;
}
esp_err_t book_collection_rename(book_collection_t *s,uint32_t id,const char *name){
    char clean[BOOK_COLLECTION_NAME_BYTES];if(!s || !s->ready || s->blocked)return ESP_ERR_INVALID_STATE;
    int index=category_index(s->data,id);if(index<0 || !book_collection_name(name,clean))return ESP_ERR_INVALID_ARG;
    if(!strcmp(s->data->categories[index].name,clean))return ESP_OK;
    for(int i=0;i<s->data->count;++i)if(!strcmp(s->data->categories[i].name,clean))return ESP_ERR_INVALID_STATE;
    book_collection_data_t *next=clone(s->data);if(!next)return ESP_ERR_NO_MEM;
    memcpy(next->categories[index].name,clean,strlen(clean)+1);return commit(s,next,true);
}
esp_err_t book_collection_remove(book_collection_t *s,uint32_t id){
    if(!s || !s->ready || s->blocked)return ESP_ERR_INVALID_STATE;
    int index=category_index(s->data,id);if(index<0)return ESP_ERR_INVALID_ARG;
    book_collection_data_t *next=clone(s->data);if(!next)return ESP_ERR_NO_MEM;
    memmove(next->categories+index,next->categories+index+1,(size_t)(next->count-index-1)*sizeof(*next->categories));--next->count;
    for(size_t i=0;i<next->used;++i)if(next->books[i].category==id)next->books[i].category=0;
    return commit(s,next,true);
}
esp_err_t book_collection_order(book_collection_t *s,uint32_t id,int direction){
    if(!s || !s->ready || s->blocked)return ESP_ERR_INVALID_STATE;
    int index=category_index(s->data,id),other=index+direction;
    if(index<0 || (direction!=1 && direction!=-1))return ESP_ERR_INVALID_ARG;
    if(other<0 || other>=s->data->count)return ESP_OK;
    book_collection_data_t *next=clone(s->data);if(!next)return ESP_ERR_NO_MEM;
    book_collection_category_t item=next->categories[index];next->categories[index]=next->categories[other];next->categories[other]=item;
    return commit(s,next,true);
}
esp_err_t book_collection_undo(book_collection_t *s){
    if(!book_collection_can_undo(s))return ESP_ERR_INVALID_STATE;
    book_collection_data_t *next=clone(s->undo);if(!next)return ESP_ERR_NO_MEM;
    if(next->next_id<s->data->next_id)next->next_id=s->data->next_id;
    return commit(s,next,false);
}
esp_err_t book_collection_export(const book_collection_t *s,FILE *file){
    if(!s || !s->ready || !file || s->blocked)return ESP_ERR_INVALID_STATE;
    size_t size=0;uint8_t *bytes=encode(s->data,s->generation?s->generation:1,&size);if(!bytes)return ESP_ERR_NO_MEM;
    esp_err_t err=fwrite(bytes,1,size,file)==size?ESP_OK:ESP_FAIL;free(bytes);return err;
}
esp_err_t book_collection_import(book_collection_t *s,FILE *file){
    if(!s || !file || !s->ready || s->blocked)return ESP_ERR_INVALID_STATE;
    book_collection_data_t *next=NULL;uint32_t generation=0;esp_err_t err=read_image(file,&next,&generation);
    return err==ESP_OK?commit(s,next,true):err;
}
