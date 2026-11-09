#!/usr/bin/env python3
"""中文：以真实书源和分配跟踪核对容量归属、解析借用、失败与重复退出。
English: Check ownership capacities, parser borrows, failures and repeated exit using real sources and tracked allocations.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile
import summarize_reader_memory as summary
from test_book_epub import make_book, make_large_book
from test_book_mobi import make_mobi

ROOT = Path(__file__).resolve().parents[1]



def test_summary(folder):
    record = 'READER_SOURCE_MEMORY v=1 event=reader_exit ' + ' '.join(
        field+'='+('3' if field == 'id' else '0') for field in summary.SOURCE_FIELDS)
    present = record.replace('event=reader_exit', 'event=present').replace('epub_text=0', 'epub_text=100')
    samples = summary.parse_source_samples(present+'\n'+record+'\n'+record)
    result = summary.summarize_source(samples)
    assert result['reader_exit']['samples'] == 2
    assert result['reader_exit']['fields']['listed_capacity']['max'] == 0
    assert result['present']['fields']['listed_capacity']['first'] == 100
    for invalid in (record.replace('v=1','v=2'), record+' id=1', record+' extra=1',
                    record.replace('zip_names=0',''), record.replace('open_files=0','open_files=2'),
                    record.replace('epub_text=0','epub_text=-1'), record.replace('id=3','id=３'),
                    record.replace('event=reader_exit','event=unknown')):
        try:
            summary.parse_source_samples(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid source record accepted: '+invalid)
    legacy = 'READER_MEMORY v=1 event=reader_exit '+' '.join(field+'=0' for field in summary.FIELDS)
    assert not summary.parse_source_samples(legacy)
    log, report = folder/'mixed.log', folder/'summary.json'
    log.write_text(legacy+'\n'+present+'\n'+record+'\n'+record)
    subprocess.run(['python3', 'tools/summarize_reader_memory.py', str(log), '--output', str(report)],
                   cwd=ROOT, check=True)
    data = json.loads(report.read_text())
    assert data['log_sha256'] == hashlib.sha256(log.read_bytes()).hexdigest()
    assert data['source_events'] == result and data['events']['reader_exit']['samples'] == 1
    print('PASS: source schema, unsigned values, duplicate/partial rejection, repeated events, legacy coexistence and log hash')


def main():
    with tempfile.TemporaryDirectory(prefix='source-memory-') as directory:
        folder = Path(directory)
        test_summary(folder)
        make_book(folder / 'small.epub', lambda files: files.update({'OPS/fonts/Example.ttf': b'font-test'}))
        make_large_book(folder / 'large.epub', 256)
        (folder / 'sample.txt').write_text('第一章 开始\n正文\n第二章 返回\n正文\n')
        make_mobi(folder / 'sample.mobi', hybrid=True)
        (folder / 'sdkconfig.h').write_text('#define CONFIG_READER_PERF 1\n#define CONFIG_READER_MEMORY 1\n')
        heap = (ROOT / 'tools/book_epub_stubs/esp_heap_caps.h').read_text()
        (folder / 'esp_heap_caps.h').write_text(heap + '\nsize_t heap_caps_get_allocated_size(void *pointer);\n')
        flags = ['cc', '-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror', '-g',
                 '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I'+str(folder),
                 '-Itools/book_epub_stubs', '-Itools/zip_host_stubs', '-Itools/book_layout_stubs',
                 '-Imain', "-Imain/diagnostics", '-Imain/book', '-Icomponents/mobi_codec/include']
        objects = []
        for name in ('book_source', 'book_txt', 'gbk', 'book_epub', 'book_index_cache',
                     'book_mobi', 'book_mobi_markup', 'palm_doc', 'book_image_header', 'zip_reader', 'html_text'):
            obj = folder / (name+'.o')
            source = ('components/mobi_codec/' if name == 'palm_doc' else
                      'main/image/' if name == 'book_image_header' else 'main/book/') + name + '.c'
            subprocess.run(flags + ['-Dmalloc=tracked_malloc', '-Dcalloc=tracked_calloc',
                                   '-Drealloc=tracked_realloc', '-Dfree=tracked_free',
                                   '-c', source, '-o', str(obj)], cwd=ROOT, check=True)
            objects.append(str(obj))
        exe = folder / 'source-memory'
        subprocess.run(flags + ['tools/reader_source_memory_host_test.c', *objects, '-lz', '-o', str(exe)],
                       cwd=ROOT, check=True)
        subprocess.run([str(exe), str(folder/'sample.txt'), str(folder/'small.epub'),
                        str(folder/'large.epub'), str(folder/'sample.mobi')], cwd=ROOT, check=True)
        # 产品和纯计时配置不得引入资源遍历符号。/ Product and timing-only configurations must not introduce resource-walk symbols.
        for config in ('#define CONFIG_READER_PERF 0\n',
                       '#define CONFIG_READER_PERF 1\n#define CONFIG_READER_MEMORY 0\n'):
            (folder/'sdkconfig.h').write_text(config)
            for name in ('book_source', 'book_epub', 'book_mobi', 'zip_reader'):
                obj = folder/(name+'-disabled.o')
                subprocess.run(flags+['-c', 'main/book/'+name+'.c', '-o', str(obj)], cwd=ROOT, check=True)
                assert 'memory_stats' not in subprocess.check_output(['nm', str(obj)], text=True)
        print('PASS: source walks absent from product and timing-only objects')


if __name__ == '__main__':
    main()
