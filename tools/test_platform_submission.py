"""Exercise OTA notes validation and submission metadata without firmware tools."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).with_name('prepare_platform_submission.py').resolve()


class SubmissionTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.notes = self.root / 'docs/ota-release-notes/1.2.3.txt'
        self.notes.parent.mkdir(parents=True)
        self.env = dict(os.environ, PICO_PROJECT_VER='1.2.3', GITHUB_REPOSITORY='test/repo',
                        GITHUB_REF_NAME='v1.2.3', GITHUB_SHA='a' * 40, GITHUB_RUN_ID='123')

    def run_script(self, *args):
        return subprocess.run([sys.executable, str(SCRIPT), *args], cwd=self.root,
                              env=self.env, capture_output=True, text=True)

    def test_missing_empty_nul_and_overlong_notes_fail(self):
        self.assertNotEqual(self.run_script('--check-notes').returncode, 0)
        for notes in ('  \n', '文字\0内容', '测' * 128):
            self.notes.write_text(notes, encoding='utf-8')
            self.assertNotEqual(self.run_script('--check-notes').returncode, 0)

    def test_valid_notes_in_submission(self):
        notes = '版本更新\n- 联网升级测试'
        self.notes.write_text(notes + '\n', encoding='utf-8')
        self.assertEqual(self.run_script('--check-notes').returncode, 0)
        files = {'bootloader/bootloader.bin': b'b', 'partition_table/partition-table.bin': b'p',
                 'ota_data_initial.bin': bytes(8192)}
        app = bytearray(288)
        app[0] = 0xe9
        app[12:14] = (0x20).to_bytes(2, 'little')
        app[32:36] = (0xabcd5432).to_bytes(4, 'little')
        app[48:53] = b'1.2.3'
        files['Read_Pico.bin'] = app
        for name, data in files.items():
            path = self.root / 'build' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        meta = json.loads((self.root / 'build/platform-submission.json').read_text())
        self.assertEqual(meta['releaseNotes'], notes)
        self.assertEqual(meta['channel'], 'stable')

    def test_beta_and_boundary(self):
        self.env['PICO_PROJECT_VER'] = '1.2.3-rc1'
        beta = self.notes.with_name('1.2.3-rc1.txt')
        beta.write_text('测' * 127 + 'ab', encoding='utf-8')
        self.assertEqual(self.run_script('--check-notes').returncode, 0)
        beta.write_text('测' * 127 + 'abc', encoding='utf-8')
        self.assertNotEqual(self.run_script('--check-notes').returncode, 0)


if __name__ == '__main__':
    unittest.main()
