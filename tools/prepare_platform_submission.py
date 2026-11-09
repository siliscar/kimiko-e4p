"""Prepare a reproducible E4P submission from one production build."""
import hashlib
import json
import os
import re
import sys
from pathlib import Path

version = os.environ['PICO_PROJECT_VER']
if not re.fullmatch(r'[0-9]{1,5}\.[0-9]{1,5}\.[0-9]{1,5}(?:-rc[0-9]{1,5})?', version):
    raise SystemExit('Invalid release version')
notes_path = Path('docs/ota-release-notes') / (version + '.txt')
try:
    notes = notes_path.read_text(encoding='utf-8').strip()
except (OSError, UnicodeError) as exc:
    raise SystemExit(f'Cannot read release notes {notes_path}: {exc}') from exc
if not notes or '\x00' in notes or len(notes.encode('utf-8')) > 383:
    raise SystemExit(f'Release notes {notes_path} must be nonempty UTF-8, at most 383 bytes, without NUL')
if sys.argv[1:] == ['--check-notes']:
    print(f'Release notes validated: {notes_path} ({len(notes.encode("utf-8"))} bytes)')
    raise SystemExit(0)
if sys.argv[1:]:
    raise SystemExit('Unsupported arguments')
files = {
    'bootloader': Path('build/bootloader/bootloader.bin'),
    'partition-table': Path('build/partition_table/partition-table.bin'),
    'app': Path('build/Read_Pico.bin'),
    'ota-data-initial': Path('build/ota_data_initial.bin'),
}
app = files['app'].read_bytes()
if len(app) < 288 or app[0] != 0xe9 or int.from_bytes(app[12:14], 'little') != 0x20:
    raise SystemExit('Not an ESP32-S31 application image')
if int.from_bytes(app[32:36], 'little') != 0xabcd5432:
    raise SystemExit('Missing application description')
if app[48:80].split(b'\0', 1)[0].decode('ascii') != version:
    raise SystemExit('Embedded application version differs from tag')
if len(app) > 0x500000:
    raise SystemExit('Application image exceeds OTA slot')
if len(files['ota-data-initial'].read_bytes()) != 8192:
    raise SystemExit('Invalid OTA data image size')
hashes = {}
for part, path in files.items():
    data = path.read_bytes()
    hashes[part] = hashlib.sha256(data).hexdigest()
    print(f'{part}: {len(data)} bytes, sha256={hashes[part]}')
meta = {
    'device': 'mogo-e4p', 'version': version,
    'channel': 'beta' if '-rc' in version else 'stable',
    'repository': os.environ['GITHUB_REPOSITORY'], 'tag': os.environ['GITHUB_REF_NAME'],
    'commit': os.environ['GITHUB_SHA'], 'runId': os.environ['GITHUB_RUN_ID'],
    'releaseNotes': notes, 'sha256': hashes,
}
Path('build/platform-submission.json').write_text(json.dumps(meta), encoding='utf-8')
