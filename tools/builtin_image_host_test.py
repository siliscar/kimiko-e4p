#!/usr/bin/env python3
# 中文：真实逐行解压与原全屏绘制四方向逐像素对比，覆盖损坏及内存不足。
# English: Compare real row decoding with original full-frame rendering in four rotations, including corruption and OOM.
from io import BytesIO
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from home_cover_cache_host_test import function
from generate_brand_images import SOURCE, outputs
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
with Image.open(SOURCE) as source:
    for path, data in outputs(source.convert('L')).items():
        if path.suffix == '.png':
            # PNG压缩字节可随zlib变化，解码像素必须一致。/ Compression bytes may vary with zlib; decoded pixels must match.
            with Image.open(path) as actual, Image.open(BytesIO(data)) as expected:
                assert actual.size == expected.size, f'Regenerate {path}'
                assert actual.convert('RGBA').tobytes() == expected.convert('RGBA').tobytes(), f'Regenerate {path}'
        else:
            assert path.read_bytes() == data, f'Regenerate {path}'
assert not (ROOT / 'assets/image/init-loading.png').exists()
assert not (ROOT / 'main/assets/loading_4bpp.bin').exists()
cmake = (ROOT / 'main/CMakeLists.txt').read_text()
assert 'loading_4bpp' not in cmake and 'assets/avatar_4bpp.bin' in cmake
with tempfile.TemporaryDirectory() as folder:
    work = Path(folder)
    command = [sys.executable, str(ROOT / 'tools/compress_builtin_images.py'),
               '--source', str(ROOT / 'main/assets'), '--output', str(work)]
    subprocess.run(command, check=True)
    assembly = ['.data']
    for name in ['lock']:
        packed_path = work / (name + '_4bpp.kbz')
        packed = packed_path.read_bytes()
        raw_path = ROOT / 'main/assets' / (name + '_4bpp.bin')
        raw = raw_path.read_bytes()
        assert struct.unpack('<4sII', packed[:12]) == (b'KBI1', len(raw), len(packed) - 12)
        assert zlib.decompress(packed[12:]) == raw
        stamp = packed_path.stat().st_mtime_ns
        subprocess.run(command, check=True)
        assert packed_path.stat().st_mtime_ns == stamp
        for symbol, path in [(f'_binary_{name}_4bpp_kbz', packed_path), (f'reference_{name}', raw_path)]:
            assembly += [f'.globl {symbol}_start', f'{symbol}_start:', f'.incbin "{path}"',
                         f'.globl {symbol}_end', f'{symbol}_end:']
    avatar_path = ROOT / 'main/assets/avatar_4bpp.bin'
    assembly += ['.globl _binary_avatar_4bpp_bin_start', '_binary_avatar_4bpp_bin_start:',
                 f'.incbin "{avatar_path}"', '.globl _binary_avatar_4bpp_bin_end', '_binary_avatar_4bpp_bin_end:']
    assert not (work / 'loading_4bpp.kbz').exists()
    (work / 'assets.S').write_text('\n'.join(assembly) + '\n')
    (work / 'epdiy.h').write_text('''#pragma once
#include <stdint.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int x,y;} Coord_xy;
enum EpdRotation {EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};
int epd_rotated_display_width(void);
int epd_rotated_display_height(void);
void epd_draw_rotated_image(EpdRect,const uint8_t*,uint8_t*);
''')
    (work / 'esp_heap_caps.h').write_text('''#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_malloc(size_t,int);
void *heap_caps_calloc(size_t,size_t,int);
void heap_caps_free(void*);
''')
    unit = r'''
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "epdiy.h"
#include "esp_heap_caps.h"
#define FRAME_BYTES (684u*1216u/2u)
static size_t live,peak;
static unsigned allocations,fail_at;
typedef union {max_align_t align;size_t bytes;} allocation_header;
void *heap_caps_malloc(size_t n,int caps){
 assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(++allocations==fail_at)return NULL;
 allocation_header *p=malloc(sizeof(*p)+n);assert(p);p->bytes=n;
 live+=n;if(live>peak)peak=live;return p+1;
}
void *heap_caps_calloc(size_t n,size_t size,int caps){
 if(size&&n>SIZE_MAX/size)return NULL;
 void *p=heap_caps_malloc(n*size,caps);if(p)memset(p,0,n*size);return p;
}
void heap_caps_free(void *p){if(p){allocation_header *h=(allocation_header*)p-1;live-=h->bytes;free(h);}}
static enum EpdRotation display_rotation;
static int epd_width(void){return 1216;}
static int epd_height(void){return 684;}
static enum EpdRotation epd_get_rotation(void){return display_rotation;}
#define _swap_int(a,b) do {int temp=(a);(a)=(b);(b)=temp;} while(0)
'''
    # 上游绘图函数保留原样，仅隔离其已有符号比较警告。/ Keep upstream drawing code verbatim and isolate its existing signed-comparison warning.
    unit += '#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wsign-compare"\n'
    epd = ROOT / 'components/epdiy/src/epdiy.c'
    for name in ['_rotate', 'epd_draw_pixel', 'epd_copy_to_framebuffer',
                 'epd_rotated_display_width', 'epd_rotated_display_height', 'epd_get_pixel',
                 'draw_rotated_transparent_image', 'epd_draw_rotated_image']:
        unit += function(name, epd) + '\n'
    unit += '#pragma GCC diagnostic pop\n#include "' + str(ROOT / 'main/ui/ui_builtin_image.c') + '"\n'
    unit += r'''
extern const uint8_t reference_lock[] asm("reference_lock_start");
static uint8_t guarded[FRAME_BYTES+32],expected[FRAME_BYTES];
static uint8_t *frame=guarded+16;
static void prepare(void){memset(guarded,0xa5,sizeof(guarded));allocations=0;assert(live==0);}
static void boundaries(void){for(unsigned i=0;i<16;++i)assert(guarded[i]==0xa5&&guarded[FRAME_BYTES+16+i]==0xa5);assert(live==0);}
static void white(void){for(size_t i=0;i<FRAME_BYTES;++i)assert(frame[i]==255);boundaries();}
static void set_word(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
int main(void){
 unsigned success_allocations=0;
 size_t lock_bytes,loading_bytes;
 assert(image_data(UI_BUILTIN_IMAGE_LOCK,&lock_bytes)==image_data(UI_BUILTIN_IMAGE_LOADING,&loading_bytes));
 assert(lock_bytes==loading_bytes);
 for(int rotation=0;rotation<4;++rotation){
  display_rotation=rotation;
  for(unsigned image=0;image<2;++image){
   prepare();assert(ui_builtin_image_size(image)==FRAME_BYTES);
   memset(expected,255,sizeof(expected));
   epd_draw_rotated_image((EpdRect){0,0,epd_rotated_display_width(),epd_rotated_display_height()},reference_lock,expected);
   assert(ui_builtin_image_draw(frame,image));assert(!memcmp(frame,expected,FRAME_BYTES));
   success_allocations=allocations;boundaries();
  }
  for(int side=60;side<=82;side+=22){
   const uint8_t *pixels=avatar_start+(side==82?60*60/2:0);
   const int positions[][2]={{0,0},{588,88},{57,199},{-5,-7},{epd_rotated_display_width()-20,epd_rotated_display_height()-20}};
   for(unsigned pos=0;pos<sizeof(positions)/sizeof(positions[0]);++pos){
    int x=positions[pos][0],y=positions[pos][1];
    prepare();memset(frame,0x99,FRAME_BYTES);memset(expected,0x99,FRAME_BYTES);
    for(int row=0;row<side;++row)for(int col=0;col<side;++col){
     uint8_t byte=pixels[(row*side+col)/2];
     uint8_t gray=((col&1)?byte>>4:byte&15)*17;
     epd_draw_pixel(x+col,y+row,gray,expected);
    }
    ui_builtin_avatar_draw(frame,x,y,side);
    assert(!memcmp(frame,expected,FRAME_BYTES));assert(!allocations);boundaries();
   }
  }
 }
 prepare();memset(frame,0x99,FRAME_BYTES);memcpy(expected,frame,FRAME_BYTES);
 ui_builtin_avatar_draw(frame,0,0,0);ui_builtin_avatar_draw(frame,0,0,61);ui_builtin_avatar_draw(NULL,0,0,60);
 assert(!memcmp(frame,expected,FRAME_BYTES));assert(!allocations);boundaries();
 assert(peak<64*1024);
 for(unsigned nth=1;nth<=success_allocations;++nth){prepare();fail_at=nth;assert(!ui_builtin_image_draw(frame,UI_BUILTIN_IMAGE_LOCK));white();}
 fail_at=0;
 size_t bytes;const uint8_t *blob=image_data(UI_BUILTIN_IMAGE_LOCK,&bytes);
 uint8_t *bad=malloc(bytes+1);assert(bad);
 for(size_t n=0;n<12;++n){prepare();assert(!draw_image(frame,blob,n));white();}
 for(size_t n=12;n<bytes;n+=251){prepare();assert(!draw_image(frame,blob,n));white();}
 for(unsigned pos=0;pos<12;++pos){memcpy(bad,blob,bytes);bad[pos]^=0x80;prepare();assert(!draw_image(frame,bad,bytes));white();}
 memcpy(bad,blob,bytes);bad[bytes-1]^=1;prepare();assert(!draw_image(frame,bad,bytes));white();
 memcpy(bad,blob,bytes);bad[bytes]=0;set_word(bad+8,(uint32_t)bytes-11);prepare();assert(!draw_image(frame,bad,bytes+1));white();
 for(int delta=-1;delta<=1;delta+=2){
  size_t raw_bytes=FRAME_BYTES+delta;uint8_t *raw=malloc(raw_bytes);assert(raw);memset(raw,0x37,raw_bytes);
  uLongf compressed_bytes=compressBound(raw_bytes);uint8_t *other=malloc(compressed_bytes+12);assert(other);
  assert(compress2(other+12,&compressed_bytes,raw,raw_bytes,9)==Z_OK);
  memcpy(other,"KBI1",4);set_word(other+4,FRAME_BYTES);set_word(other+8,(uint32_t)compressed_bytes);
  prepare();assert(!draw_image(frame,other,compressed_bytes+12));white();free(other);free(raw);
 }
 prepare();assert(!ui_builtin_image_draw(frame,99));white();assert(ui_builtin_image_size(99)==0);
 assert(!ui_builtin_image_draw(NULL,UI_BUILTIN_IMAGE_LOCK));assert(!live);free(bad);
 printf("PASS: splash/lock share one resource; generated art and both avatars match pixels in four rotations, including clipping; bounded temporary heap peak %zu B; OOM, bad headers, truncation, checksum, trailing data and output-size faults clear white without leaks or overwrites\n",peak);
}
'''
    (work / 'test.c').write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g',
                    '-fsanitize=address,undefined', '-I' + str(work),
                    '-I' + str(ROOT / 'main/ui'), str(work / 'test.c'),
                    str(work / 'assets.S'), '-lz', '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
