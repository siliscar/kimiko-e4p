#!/usr/bin/env python3
"""中文：汇总诊断资源容量与退出回收范围，不自动判定泄漏。
English: Summarize diagnostic allocation capacities and exit ranges without declaring leaks.
"""
import argparse
import hashlib
import json
from pathlib import Path

FIELDS = ('id font_source font_tables font_blocks font_glyphs font_work font_catalog '
          'layout chapter prefetch background page_images shelf_ui font_slots prefetch_worker '
          'prefetch_stack_free internal_free internal_largest psram_free psram_largest ui_stack_free').split()
EVENTS = ('present', 'book_closed', 'reader_exit')


def parse_samples(text):
    samples = []
    for number, line in enumerate(text.splitlines(), 1):
        _, marker, payload = line.partition('READER_MEMORY ')
        if not marker:
            continue
        try:
            words = payload.split()
            row = dict(word.split('=', 1) for word in words)
            if len(row) != len(words) or row.get('v') != '1' or row.get('event') not in EVENTS:
                raise ValueError('schema, duplicate key or event')
            if set(row) != set(FIELDS) | {'v', 'event'}:
                raise ValueError('missing or extra fields')
            for key in FIELDS:
                value = row[key]
                if not value.isascii() or not value.isdecimal():
                    raise ValueError('unsigned decimal required')
                row[key] = int(value)
            if row['prefetch_worker'] not in (0, 1):
                raise ValueError('invalid worker state')
            samples.append(row)
        except ValueError as error:
            raise ValueError(f'Invalid READER_MEMORY record on line {number}: {error}') from error
    if not samples:
        raise ValueError('No READER_MEMORY records')
    return samples


def summarize(samples):
    result = {}
    for event in EVENTS:
        rows = [row for row in samples if row['event'] == event]
        if not rows:
            continue
        result[event] = {'samples': len(rows), 'fields': {}}
        for key in FIELDS:
            if key == 'id':
                continue
            values = [row[key] for row in rows]
            result[event]['fields'][key] = {
                'first': values[0], 'last': values[-1], 'min': min(values), 'max': max(values),
            }
    return result



SOURCE_FIELDS = ('id txt_index mobi_state mobi_index epub_state epub_index epub_text epub_faces zip_state '
                 'zip_index zip_names zip_directory open_files').split()


def parse_source_samples(text):
    samples = []
    for number, line in enumerate(text.splitlines(), 1):
        _, marker, payload = line.partition('READER_SOURCE_MEMORY ')
        if not marker:
            continue
        try:
            words = payload.split()
            row = dict(word.split('=', 1) for word in words)
            if len(row) != len(words) or row.get('v') != '1' or row.get('event') not in EVENTS:
                raise ValueError('schema, duplicate key or event')
            legacy = (set(SOURCE_FIELDS) - {'mobi_state', 'mobi_index'}) | {'v', 'event'}
            if set(row) == legacy:
                row.update(mobi_state='0', mobi_index='0')
            if set(row) != set(SOURCE_FIELDS) | {'v', 'event'}:
                raise ValueError('missing or extra fields')
            for key in SOURCE_FIELDS:
                value = row[key]
                if not value.isascii() or not value.isdecimal():
                    raise ValueError('unsigned decimal required')
                row[key] = int(value)
            if row['open_files'] not in (0, 1):
                raise ValueError('invalid current-source file count')
            samples.append(row)
        except ValueError as error:
            raise ValueError(f'Invalid READER_SOURCE_MEMORY record on line {number}: {error}') from error
    return samples


def summarize_source(samples):
    result = {}
    for event in EVENTS:
        rows = [row for row in samples if row['event'] == event]
        if not rows:
            continue
        fields = {}
        for key in [*SOURCE_FIELDS[1:], 'listed_capacity']:
            values = [sum(row[field] for field in SOURCE_FIELDS[1:-1])
                      if key == 'listed_capacity' else row[key] for row in rows]
            fields[key] = {'first': values[0], 'last': values[-1],
                           'min': min(values), 'max': max(values)}
        result[event] = {'samples': len(rows), 'fields': fields}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    raw = args.log.read_bytes()
    result = {'schema': 1, 'log': str(args.log.resolve()),
              'log_sha256': hashlib.sha256(raw).hexdigest(),
              'events': summarize(parse_samples(raw.decode('utf-8', errors='replace')))}
    source_samples = parse_source_samples(raw.decode('utf-8', errors='replace'))
    if source_samples:
        result['source_events'] = summarize_source(source_samples)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    for event, data in result['events'].items():
        print(f'{event}: {data["samples"]} samples')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
