#!/usr/bin/env python3
"""中文：生成自有 MOBI 夹具，验证原生后端；外部样书仅显式本机读取。
English: Generate original MOBI fixtures and test the native backend; external samples are opt-in/local.
"""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def png(width, height):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 0, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress((b'\0' + bytes([128]) * width) * height)) + chunk(b'IEND', b''))


def palmdoc(raw):
    out = bytearray()
    for at in range(0, len(raw), 8):
        part = raw[at:at+8]
        out.append(len(part))
        out.extend(part)
    return bytes(out)


def make_mobi(path, *, compression=2, encoding=65001, hybrid=False, extra=3,
              raw=None, chunk=73, version=6, encrypted=False, mutate=None):
    if raw is None:
        template = ('<html><head><guide><reference type="toc" filepos="{toc:010d}"/></guide></head><body>'
                    '<p>Front matter</p><mbp:pagebreak/>'
                    '<p id="one">Chapter 1</p><p>Caf\u00e9 \u20ac &amp; words '
                    '<a filepos="{note:010d}">[1]</a></p><img recindex="1"/>'
                    '<p id="note">A readable footnote.</p><mbp:pagebreak/><p id="two">Chapter 2</p>'
                    '<p>Last paragraph.</p><mbp:pagebreak/><div id="toc">'
                    '<a filepos="{one:010d}">Chapter 1</a><br/>'
                    '<a filepos="{two:010d}">Chapter 2</a></div></body></html>')
        codec = 'utf-8' if encoding == 65001 else 'cp1252'
        initial = template.format(toc=0, note=0, one=0, two=0).encode(codec)
        positions = {name: initial.index(needle) for name, needle in
                     [('one', b'<p id="one">'), ('two', b'<p id="two">'),
                      ('note', b'<p id="note">'), ('toc', b'<div id="toc">')]}
        raw = template.format(**positions).encode(codec)
    records = [raw[i:i+chunk] for i in range(0, len(raw), chunk)]
    count = len(records)
    first_image = count + 1
    exth_values = [(100, b'Original Author'), (503, b'Original Fixture'), (201, struct.pack('>I', 1))]
    if hybrid:
        exth_values.append((121, struct.pack('>I', count + 4)))
    body = b''.join(struct.pack('>II', key, len(value)+8) + value for key, value in exth_values)
    exth = b'EXTH' + struct.pack('>II', len(body)+12, len(exth_values)) + body
    header = bytearray(16 + 264)
    struct.pack_into('>HHIHHHH', header, 0, compression, 0, len(raw), count, 4096, int(encrypted), 0)
    header[16:20] = b'MOBI'
    for at, value in [(20,264),(24,2),(28,encoding),(36,version),(108,first_image),(128,0x40)]:
        struct.pack_into('>I', header, at, value)
    struct.pack_into('>H', header, 242, extra)
    header.extend(exth)
    encoded = []
    for record in records:
        data = palmdoc(record) if compression == 2 else record
        if extra == 3:
            data += b'\0xy\x83'
        elif extra:
            raise ValueError('fixture extra flag unsupported')
        encoded.append(data)
    payload = [bytes(header), *encoded, png(17,23), png(53,71)]
    if hybrid:
        kf8 = bytearray(header)
        struct.pack_into('>I', kf8, 36, 8)
        payload += [b'BOUNDARY', bytes(kf8), b'KF8 TEXT MUST NOT APPEAR']
    pdb = bytearray(78)
    pdb[:12] = b'MOBI fixture'
    pdb[60:68] = b'BOOKMOBI'
    struct.pack_into('>H', pdb, 76, len(payload))
    offset = len(pdb) + len(payload)*8 + 2
    table = bytearray()
    for i, record in enumerate(payload):
        table += struct.pack('>I', offset) + bytes([0,0,0,(i+1)&255])
        offset += len(record)
    result = pdb + table + b'\0\0' + b''.join(payload)
    if mutate:
        mutate(result, len(pdb) + len(table) + 2)
    Path(path).write_bytes(result)
    return raw


def compile_test(folder):
    compiler = os.environ.get('CC', 'cc')
    flags = ['-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror', '-g',
             '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    modern = Path('/Library/Developer/CommandLineTools/usr/bin/clang')
    if sys.platform == 'darwin' and 'CC' not in os.environ and modern.exists():
        compiler = str(modern)
        flags += ['-isysroot', '/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk']
    exe = folder / 'mobi-test'
    subprocess.run([compiler, *flags, '-Itools/book_epub_stubs', '-Itools/zip_host_stubs',
                    '-Itools/book_layout_stubs', '-Icomponents/mobi_codec/include', '-Imain/book',
                    'tools/book_mobi_host_test.c', 'main/book/book_mobi_markup.c',
                    'components/mobi_codec/palm_doc.c',
                    'main/book/html_text.c', 'main/image/book_image_header.c', '-o', str(exe)], cwd=ROOT, check=True)
    return exe


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sample', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='mogo-mobi-') as temp:
        folder = Path(temp)
        exe = compile_test(folder)
        for compression, encoding, hybrid in [(1,65001,False), (2,65001,False), (2,1252,False), (2,65001,True)]:
            path = folder / 'fixture.mobi'
            expected = make_mobi(path, compression=compression, encoding=encoding, hybrid=hybrid)
            subprocess.run([str(exe), 'fixture', str(path)], check=True)
            actual = subprocess.check_output([str(exe), 'raw', str(path)])
            assert actual == expected
        for name, options in [('kf8', {'version':8}), ('drm', {'encrypted':True}),
                              ('huff', {'compression':17480}), ('encoding', {'encoding':932, 'raw':b'<p>test</p>'})]:
            path = folder / (name+'.mobi')
            make_mobi(path, **options)
            subprocess.run([str(exe), 'unsupported', str(path)], check=True)
        for name, change in [
            ('offset', lambda b,h: struct.pack_into('>I', b, 78, len(b)+1)),
            ('text_total', lambda b,h: struct.pack_into('>I', b, h+4, 0xffffffff)),
            ('exth_length', lambda b,h: struct.pack_into('>I', b, h+284, 0xffffffff)),
            ('backref', lambda b,h: b.__setitem__(slice(struct.unpack_from('>I', b, 86)[0], struct.unpack_from('>I', b, 86)[0]+2), b'\x80\0')),
            ('extra', lambda b,h: b.__setitem__(struct.unpack_from('>I', b, 94)[0]-1, 0x7f)),
            ('metadata_bounds', lambda b,h: struct.pack_into('>II', b, h+84, 0xffffff00, 999)),
        ]:
            path = folder / (name+'.mobi')
            make_mobi(path, mutate=change)
            subprocess.run([str(exe), 'invalid', str(path)], check=True)
        for name, raw in [('nul', b'<p>bad\0text</p>'), ('utf8', b'<p>bad\xc0\xaf</p>'),
                          ('utf8_truncated', b'<p>bad\xf0\x90'),
                          ('segments', b'<p>text</p><mbp:pagebreak/>'*2050),
                          ('navigation', b'<h1>Title</h1>'*2050)]:
            path = folder / (name+'.mobi')
            make_mobi(path, raw=raw, chunk=4096)
            subprocess.run([str(exe), 'invalid', str(path)], check=True)
        for length in [0, 77, 120]:
            path = folder / 'truncated.mobi'
            make_mobi(path)
            with path.open('r+b') as stream:
                stream.truncate(length)
            subprocess.run([str(exe), 'invalid', str(path)], check=True)
        path = folder / ('Book ' + '字'*70 + '.mobi')
        make_mobi(path)
        subprocess.run([str(exe), 'fixture', str(path)], check=True)
        path = folder/'long.mobi'
        raw = b'<html><body>' + b'<p>Repeated paragraph, always readable.</p>'*9000 + b'</body></html>'
        make_mobi(path, raw=raw, chunk=4096)
        subprocess.run([str(exe), 'long', str(path)], check=True)
        path = folder/'oversize.mobi'
        make_mobi(path, raw=b'<p>'+b'x'*(256*1024)+b'</p>', chunk=4096)
        subprocess.run([str(exe), 'invalid', str(path)], check=True)
        subprocess.run([str(exe), 'codec'], check=True)
        subprocess.run([sys.executable, 'tools/mobi_product_host_test.py'], cwd=ROOT, check=True)
        if args.sample:
            subprocess.run([str(exe), 'sample', str(args.sample)], check=True)
            raw = subprocess.check_output([str(exe), 'raw', str(args.sample)])
            digest = hashlib.sha256(raw).hexdigest()
            assert digest == 'ade7219d83bb7f0189480f8ee7f58a79fd52797ab9150d14714ff1bae9407ca3'
            print('PASS: external sample MOBI6 raw SHA-256', digest)
        print('PASS: native MOBI fixtures, compression, encodings, hybrids, malformed headers, limits and codec')


if __name__ == '__main__':
    main()
