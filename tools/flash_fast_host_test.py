#!/usr/bin/env python3
"""中文：快速刷机的无设备回归，使用内存 Flash 与真实进程超时。
English: Offline fast-flash regressions with in-memory flash and real process timeouts.
"""

import binascii
import contextlib
import json
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import flash_fast as fast


def selection(*records):
    data = bytearray(b"\xff" * (2 * fast.SECTOR))
    for copy, (sequence, state) in enumerate(records):
        crc = binascii.crc32(struct.pack("<I", sequence), 0xFFFFFFFF) & 0xFFFFFFFF
        struct.pack_into("<I20xII", data, copy * fast.SECTOR, sequence, state, crc)
    return bytes(data)


class MemoryDevice:
    def __init__(self, bundle, selected=selection((1, 2))):
        self.mac = "1c:29:04:d0:48:e0"
        self.memory = bytearray(b"\xff" * 0x100000)
        self.bundle = bundle
        self.writes = []
        self.fail_write = False
        self.corrupt_write = False
        self.fail_boot = False
        self.closed = False
        self.opened = True
        self.boot_attempted = False
        self.recovered = False
        self.set(bundle["table_offset"], bundle["table"])
        self.set(bundle["boot_offset"], bundle["boot"])
        self.set(bundle["otadata"].offset, selected)
        self.set(0xF8000, b"USER SETTINGS AND BOOKS")

    def set(self, offset, data):
        self.memory[offset:offset + len(data)] = data

    def read(self, offset, size):
        return bytes(self.memory[offset:offset + size])

    def md5(self, offset, size):
        return fast.digest(self.read(offset, size), "md5")

    def write(self, pairs, settings):
        for address, data in pairs:
            self.writes.append((address, len(data)))
            if self.fail_write:
                raise fast.FlashError("connection lost during write")
            if self.corrupt_write:
                data = b"x" + data[1:]
            self.set(address, data)

    def check_bootloader(self, bundle):
        fast.require(self.read(bundle["boot_offset"], len(bundle["boot"])) == bundle["boot"],
                     "Bootloader base compatibility differs")
        return {"matches_build": True}

    def boot(self, bundle, offset, budget, output):
        self.boot_attempted = True
        if self.fail_boot:
            raise fast.FlashError("expected UI did not boot")
        return {"slot": True, "version": True, "elf": True, "ui_ready": True}

    def connect(self):
        pass

    def recover(self, budget, output):
        self.recovered = True
        self.boot_attempted = True
        return {"application": True, "version": True, "ui_ready": True}

    def close(self):
        self.closed = True
        self.opened = False


class FlashTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="e4p-flash-test-")
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.store_patch = patch.object(fast, "STORE", self.folder)
        self.store_patch.start()
        self.addCleanup(self.store_patch.stop)
        self.start_patch = patch.object(fast, "STARTED", time.monotonic())
        self.start_patch.start()
        self.addCleanup(self.start_patch.stop)
        self.slots = [SimpleNamespace(name="ota_0", offset=0x10000, size=0x30000),
                      SimpleNamespace(name="ota_1", offset=0x40000, size=0x30000)]
        image = b"a" * fast.SECTOR + b"b" * fast.SECTOR + b"c" * fast.SECTOR + b"\xff" * fast.SECTOR
        self.bundle = {"padded": image, "image": image, "table": b"PARTITION-TABLE",
                       "table_offset": 0x8000, "boot": b"BOOTLOADER", "boot_offset": 0x2000,
                       "otadata": SimpleNamespace(offset=0xD000, size=2 * fast.SECTOR),
                       "slots": self.slots, "settings": {"flash_mode": "dio", "flash_size": "16MB", "flash_freq": "80m"},
                       "version": "1.0.3", "image_sha256": fast.digest(image),
                       "elf_sha256": "abcdef1234" * 6 + "abcd"}
        self.receipt = {"status": "running", "write_started": False, "flash_verified": False}

    def execute(self, device, budget=None):
        return fast.flash_bundle(self.bundle, device, "1c:29:04:d0:48:e0", budget or fast.Budget(),
                                 self.folder, self.receipt)

    def test_select_both_ota_slots_from_sequence(self):
        for sequence in range(1, 7):
            with self.subTest(sequence=sequence):
                self.assertIs(fast.active_slot(selection((sequence, 2)), self.slots), self.slots[(sequence - 1) % 2])

    def test_select_newest_valid_copy(self):
        self.assertIs(fast.active_slot(selection((4, 2), (5, 2)), self.slots), self.slots[0])

    def test_pending_and_unverified_selections_are_rejected(self):
        for state in (0, 1, 0xFFFFFFFF):
            with self.subTest(state=state), self.assertRaises(fast.FlashError):
                fast.active_slot(selection((4, 2), (5, state)), self.slots)

    def test_aborted_and_invalid_records_do_not_override_valid(self):
        for state in (3, 4):
            self.assertIs(fast.active_slot(selection((4, 2), (5, state)), self.slots), self.slots[1])

    def test_bad_crc_empty_zero_and_short_selection_are_rejected(self):
        bad_crc = bytearray(selection((1, 2)))
        bad_crc[28] ^= 1
        for data in (bytes(bad_crc), selection(), selection((0, 2)), b"short"):
            with self.subTest(data=data[:32]), self.assertRaises(fast.FlashError):
                fast.active_slot(data, self.slots)

    def test_diff_merges_whole_adjacent_sectors(self):
        old = b"a" * 8 * fast.SECTOR
        new = bytearray(old)
        new[fast.SECTOR + 1] = 98
        new[2 * fast.SECTOR + 5] = 98
        spans = fast.changed_spans(old, bytes(new))
        self.assertEqual(spans, [(fast.SECTOR, bytes(new[fast.SECTOR:3 * fast.SECTOR]))])

    def test_missing_or_large_delta_writes_full_app_only(self):
        new = self.bundle["padded"]
        self.assertEqual(fast.changed_spans(None, new), [(0, new)])
        self.assertEqual(fast.changed_spans(b"x" * len(new), new), [(0, new)])

    def test_many_spans_collapse_to_app_write(self):
        old = b"a" * fast.SECTOR * 100
        new = bytearray(old)
        for sector in range(0, 50, 2):
            new[sector * fast.SECTOR] = 98
        self.assertEqual(fast.changed_spans(old, bytes(new)), [(0, bytes(new))])

    def test_shrinking_image_keeps_sector_boundaries(self):
        new = self.bundle["padded"]
        self.assertEqual(fast.changed_spans(new + b"z" * fast.SECTOR, new), [])
        with self.assertRaises(fast.FlashError):
            fast.changed_spans(new, b"unaligned")

    def test_full_app_write_protects_every_other_flash_byte(self):
        for record, index in (((1, 2), 0), ((2, 2), 1)):
            with self.subTest(index=index):
                device = MemoryDevice(self.bundle, selection(record))
                before = bytes(device.memory)
                self.execute(device)
                start = self.slots[index].offset
                end = start + len(self.bundle["padded"])
                self.assertEqual(device.memory[:start], before[:start])
                self.assertEqual(device.memory[end:], before[end:])
                self.assertEqual(device.read(start, end - start), self.bundle["padded"])
                self.assertEqual(device.writes, [(start, end - start)])

    def test_verified_baseline_enables_small_delta(self):
        device = MemoryDevice(self.bundle)
        old = bytearray(self.bundle["padded"])
        old[fast.SECTOR + 3] = 120
        device.set(self.slots[0].offset, old)
        (self.folder / "1c2904d048e0-10000.bin").write_bytes(old)
        self.execute(device)
        self.assertEqual(device.writes, [(0x10000 + fast.SECTOR, fast.SECTOR)])
        self.assertTrue(self.receipt["baseline_verified"])

    def test_stale_baseline_falls_back_to_selected_app(self):
        device = MemoryDevice(self.bundle)
        (self.folder / "1c2904d048e0-10000.bin").write_bytes(self.bundle["padded"])
        self.execute(device)
        self.assertFalse(self.receipt["baseline_verified"])
        self.assertEqual(device.writes, [(0x10000, len(self.bundle["padded"]))])

    def test_already_installed_image_is_verified_without_writes(self):
        device = MemoryDevice(self.bundle)
        device.set(0x10000, self.bundle["padded"])
        self.execute(device)
        self.assertEqual(device.writes, [])
        self.assertTrue(self.receipt["flash_verified"])

    def test_explicit_rewrite_exercises_real_write_even_when_image_matches(self):
        device = MemoryDevice(self.bundle)
        device.set(0x10000, self.bundle["padded"])
        fast.flash_bundle(self.bundle, device, device.mac, fast.Budget(), self.folder, self.receipt, rewrite_app=True)
        self.assertEqual(device.writes, [(0x10000, len(self.bundle["padded"]))])
        self.assertTrue(self.receipt["flash_verified"])

    def test_identity_layout_and_bootloader_mismatch_write_nothing(self):
        for mismatch in ("mac", "table", "boot"):
            with self.subTest(mismatch=mismatch):
                device = MemoryDevice(self.bundle)
                if mismatch == "mac":
                    device.mac = "00:00:00:00:00:00"
                else:
                    device.set(self.bundle["table_offset" if mismatch == "table" else "boot_offset"], b"X")
                with self.assertRaises(fast.FlashError):
                    self.execute(device)
                self.assertEqual(device.writes, [])

    def test_insufficient_time_never_starts_write(self):
        device = MemoryDevice(self.bundle)
        with self.assertRaises(fast.FlashError):
            self.execute(device, fast.Budget(1))
        self.assertFalse(self.receipt["write_started"])
        self.assertEqual(device.writes, [])

    def test_extended_budget_accepts_full_app_after_preflight(self):
        image = b"a" * 4523184
        self.bundle["padded"] = image + b"\xff" * (-len(image) % fast.SECTOR)
        self.slots[0].size = 0x500000
        device = MemoryDevice(self.bundle)
        with patch.object(fast.time, "monotonic", return_value=fast.STARTED + 35):
            with self.assertRaisesRegex(fast.FlashError, "Insufficient time"):
                self.execute(device, fast.Budget(110 - fast.CLEANUP_SECONDS))
            self.assertEqual(device.writes, [])
            self.assertFalse(self.receipt["write_started"])
            self.execute(device, fast.Budget(fast.LIMIT - fast.CLEANUP_SECONDS))
        self.assertTrue(self.receipt["flash_verified"])
        self.assertEqual(device.writes, [(self.slots[0].offset, len(self.bundle["padded"]))])

    def test_write_and_digest_failure_cannot_be_reported_as_success(self):
        for attribute in ("fail_write", "corrupt_write"):
            with self.subTest(attribute=attribute):
                device = MemoryDevice(self.bundle)
                setattr(device, attribute, True)
                with self.assertRaises(fast.FlashError):
                    self.execute(device)
                self.assertTrue(self.receipt["write_started"])
                self.assertFalse(self.receipt["flash_verified"])
                self.assertFalse((self.folder / "1c2904d048e0-10000.bin").exists())

    def test_boot_failure_preserves_verified_flash_state(self):
        device = MemoryDevice(self.bundle)
        device.fail_boot = True
        with self.assertRaises(fast.FlashError):
            self.execute(device)
        receipt = json.loads((self.folder / "result.json").read_text())
        self.assertTrue(receipt["flash_verified"])
        self.assertNotIn("boot", receipt)

    def test_boot_requires_expected_slot_version_elf_and_ui(self):
        text = "Loaded app from partition at offset 0x00010000\nApp version: 1.0.3\nELF file SHA256: abcdef123...\nUI ready on Home\n"
        self.assertTrue(all(fast.boot_checks(text, self.bundle, 0x10000).values()))
        for previous, wrong in (("0x00010000", "0x510000"), ("1.0.3", "1.0.2"),
                                ("abcdef123", "000000000"), ("UI ready on", "starting")):
            self.assertFalse(all(fast.boot_checks(text.replace(previous, wrong), self.bundle, 0x10000).values()))

    def test_mac_normalization_rejects_missing_or_partial_identity(self):
        self.assertEqual(fast.normalize_mac("1C:29:04:D0:48:E0"), "1c:29:04:d0:48:e0")
        self.assertEqual(fast.normalize_mac("1C2904D048E0"), "1c:29:04:d0:48:e0")
        for value in (None, "", "1c:29", "not-a-device"):
            with self.assertRaises(fast.FlashError):
                fast.normalize_mac(value)

    def test_real_subprocess_timeout_returns_quickly(self):
        started = time.monotonic()
        with self.assertRaises(subprocess.TimeoutExpired):
            fast.run([sys.executable, "-c", "import time; time.sleep(30)"], fast.Budget(), seconds=0.05)
        self.assertLess(time.monotonic() - started, 2)

    def test_source_fingerprint_includes_untracked_modified_and_deleted_inputs(self):
        root = self.folder / "repo"
        (root / "main").mkdir(parents=True)
        subprocess.run(["git", "init", "-q", str(root)], check=True)
        tracked = root / "main/app.c"
        tracked.write_text("original")
        subprocess.run(["git", "-C", str(root), "add", "main/app.c"], check=True)
        with patch.object(fast, "ROOT", root):
            original = fast.source_digest(fast.Budget())
            tracked.write_text("modified")
            modified = fast.source_digest(fast.Budget())
            self.assertNotEqual(original, modified)
            (root / "main/new.h").write_text("new input")
            added = fast.source_digest(fast.Budget())
            self.assertNotEqual(modified, added)
            tracked.unlink()
            self.assertNotEqual(added, fast.source_digest(fast.Budget()))

    @contextlib.contextmanager
    def main_mocks(self, mode, device=None):
        with contextlib.ExitStack() as stack:
            stack.enter_context(patch.object(sys, "argv", ["flash_fast.py", mode, "--yes"]))
            stack.enter_context(patch.object(fast.signal, "signal"))
            stack.enter_context(patch.object(fast.signal, "setitimer"))
            stack.enter_context(patch.object(fast, "select_build", return_value=(self.folder, {}, {})))
            build = stack.enter_context(patch.object(fast, "build_product", return_value="current-source"))
            stack.enter_context(patch.object(fast, "load_bundle", return_value=self.bundle))
            stack.enter_context(patch.object(fast, "source_digest", return_value="current-source"))
            stack.enter_context(patch.object(fast, "select_port", return_value=("/dev/test-only", "1c:29:04:d0:48:e0")))
            stack.enter_context(patch.object(fast.shutil, "which", return_value=None))
            opener = stack.enter_context(patch.object(fast, "Device", return_value=device))
            yield build, opener

    def test_prepare_does_not_open_device(self):
        with self.main_mocks("prepare") as (_, opener):
            self.assertEqual(fast.main(), 0)
            opener.assert_not_called()
        receipt = json.loads(next(self.folder.glob("*/result.json")).read_text())
        self.assertEqual(receipt["status"], "success")
        self.assertFalse(receipt["device_opened"])
        self.assertFalse(receipt["write_started"])

    def test_main_success_closes_serial_and_records_actual_write_ranges(self):
        device = MemoryDevice(self.bundle)
        with self.main_mocks("flash", device):
            self.assertEqual(fast.main(), 0)
        self.assertTrue(device.closed)
        receipt = json.loads(next(self.folder.glob("*/result.json")).read_text())
        self.assertEqual(receipt["status"], "success")
        self.assertTrue(receipt["serial_released"])
        self.assertEqual(receipt["write_ranges"][0]["offset"], 0x10000)
        self.assertTrue(receipt["flash_verified"])

    def test_main_write_failure_closes_serial_and_records_uncertain_flash(self):
        device = MemoryDevice(self.bundle)
        device.fail_write = True
        with self.main_mocks("flash", device), self.assertRaises(fast.FlashError):
            fast.main()
        self.assertTrue(device.closed)
        receipt = json.loads(next(self.folder.glob("*/result.json")).read_text())
        self.assertEqual(receipt["status"], "failed")
        self.assertTrue(receipt["write_started"])
        self.assertFalse(receipt["flash_verified"])
        self.assertTrue(receipt["serial_released"])
        self.assertFalse(device.recovered)
        self.assertEqual(receipt["recovery"]["status"], "required")

    def test_preflight_read_failure_restores_boot_without_retrying_flash(self):
        device = MemoryDevice(self.bundle)
        with self.main_mocks("flash", device), patch.object(device, "check_bootloader", side_effect=fast.FlashError("read failed")) as check:
            with self.assertRaises(fast.FlashError):
                fast.main()
            check.assert_called_once()
        receipt = json.loads(next(self.folder.glob("*/result.json")).read_text())
        self.assertTrue(device.recovered)
        self.assertTrue(device.closed)
        self.assertFalse(receipt["write_started"])
        self.assertEqual(receipt["status"], "failed")
        self.assertEqual(receipt["recovery"]["status"], "success")

    def test_failed_connection_also_restores_boot_and_releases_serial(self):
        device = MemoryDevice(self.bundle)
        with self.main_mocks("flash", device), patch.object(device, "connect", side_effect=fast.FlashError("stub upload failed")):
            with self.assertRaises(fast.FlashError):
                fast.main()
        receipt = json.loads(next(self.folder.glob("*/result.json")).read_text())
        self.assertTrue(device.recovered)
        self.assertTrue(receipt["device_opened"])
        self.assertTrue(receipt["serial_released"])

    def test_timeout_restores_untouched_device_inside_reserved_budget(self):
        device = MemoryDevice(self.bundle)
        with self.main_mocks("flash", device), patch.object(fast, "flash_bundle", side_effect=fast.FlashError("time budget reached")), patch.object(device, "recover", wraps=device.recover) as recover:
            with self.assertRaises(fast.FlashError):
                fast.main()
            budget = recover.call_args.args[0]
            self.assertEqual(budget.end, fast.STARTED + fast.LIMIT)
        self.assertTrue(device.closed)
        self.assertTrue(device.recovered)

    def test_real_alarm_interrupts_work_and_still_restores_boot(self):
        device = MemoryDevice(self.bundle)
        real_signal, real_timer = signal.signal, signal.setitimer
        handlers = {number: signal.getsignal(number) for number in (signal.SIGALRM, signal.SIGTERM)}

        def expire(*args):
            real_timer(signal.ITIMER_REAL, 0.01)
            time.sleep(2)

        try:
            with self.main_mocks("flash", device), patch.object(fast.signal, "signal", wraps=real_signal), patch.object(fast.signal, "setitimer", wraps=real_timer), patch.object(fast, "flash_bundle", side_effect=expire):
                with self.assertRaisesRegex(fast.FlashError, "time budget reached"):
                    fast.main()
        finally:
            real_timer(signal.ITIMER_REAL, 0)
            for number, handler in handlers.items():
                real_signal(number, handler)
        receipt = json.loads(next(self.folder.glob("*/result.json")).read_text())
        self.assertTrue(device.recovered)
        self.assertTrue(device.closed)
        self.assertFalse(receipt["write_started"])
        self.assertEqual(receipt["recovery"]["status"], "success")

    def test_recovery_failure_is_recorded_and_port_still_closed(self):
        device = MemoryDevice(self.bundle)
        with self.main_mocks("flash", device), patch.object(device, "connect", side_effect=fast.FlashError("connect failed")), patch.object(device, "recover", side_effect=fast.FlashError("device disconnected")):
            with self.assertRaisesRegex(fast.FlashError, "connect failed"):
                fast.main()
        receipt = json.loads(next(self.folder.glob("*/result.json")).read_text())
        self.assertEqual(receipt["recovery"]["status"], "failed")
        self.assertEqual(receipt["error"], "connect failed")
        self.assertTrue(receipt["serial_released"])

    def test_failed_boot_is_not_repeated(self):
        device = MemoryDevice(self.bundle)
        device.fail_boot = True
        with self.main_mocks("flash", device):
            with self.assertRaises(fast.FlashError):
                fast.main()
        self.assertFalse(device.recovered)
        self.assertTrue(device.closed)

    def test_device_uses_official_stub_and_never_falls_back_to_rom(self):
        device = fast.Device.__new__(fast.Device)
        rom = Mock(IMAGE_CHIP_ID=42, secure_download_mode=False)
        rom.get_security_info.return_value = {"chip_id": 42, "parsed_flags": {"SECURE_BOOT_EN": False}}
        rom.get_flash_encryption_enabled.return_value = False
        rom.read_mac.return_value = bytes.fromhex("1c2904d048e0")
        device.serial = Mock()
        device.esp = rom
        device.cmds = Mock()
        device.cmds.run_stub.return_value.IS_STUB = True
        device.cmds.detect_flash_size.return_value = "16MB"
        device.connect()
        device.cmds.run_stub.assert_called_once_with(rom)
        device.esp.change_baud.assert_called_once_with(460800)
        device.cmds.attach_flash.assert_called_once_with(device.esp)
        self.assertEqual(device.mac, "1c:29:04:d0:48:e0")

    def test_unavailable_stub_is_rejected_before_flash_operations(self):
        device = fast.Device.__new__(fast.Device)
        device.serial = Mock()
        device.esp = Mock(IMAGE_CHIP_ID=42, secure_download_mode=False)
        device.esp.get_security_info.return_value = {"chip_id": 42, "parsed_flags": {"SECURE_BOOT_EN": False}}
        device.esp.get_flash_encryption_enabled.return_value = False
        device.esp.read_mac.return_value = bytes.fromhex("1c2904d048e0")
        device.cmds = Mock()
        device.cmds.run_stub.return_value.IS_STUB = False
        with self.assertRaisesRegex(fast.FlashError, "no ROM fallback"):
            device.connect()
        device.cmds.attach_flash.assert_not_called()

    def test_build_failure_never_opens_device(self):
        with self.main_mocks("flash") as (build, opener):
            build.side_effect = fast.FlashError("incremental build exceeded 20s")
            with self.assertRaises(fast.FlashError):
                fast.main()
            opener.assert_not_called()
        receipt = json.loads(next(self.folder.glob("*/result.json")).read_text())
        self.assertFalse(receipt["device_opened"])
        self.assertFalse(receipt["write_started"])

    def test_flash_without_confirmation_and_excessive_timeout_are_rejected(self):
        script = Path(fast.__file__).resolve()
        for arguments in (("flash",), ("status", "--timeout", "0"),
                          ("status", "--timeout", "-1"), ("status", "--timeout", "181")):
            result = subprocess.run([sys.executable, str(script), *arguments], capture_output=True, timeout=3)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn(b"Connect S31", result.stdout)

    def test_default_and_explicit_extended_timeouts_keep_cleanup_reserve(self):
        for timeout in (None, 111, 180):
            with self.subTest(timeout=timeout):
                device = MemoryDevice(self.bundle)
                arguments = ["flash_fast.py", "flash", "--yes"]
                if timeout is not None:
                    arguments.extend(["--timeout", str(timeout)])
                with self.main_mocks("flash", device), patch.object(sys, "argv", arguments), \
                        patch.object(fast, "STORE", self.folder / str(timeout)), \
                        patch.object(fast, "flash_bundle", wraps=fast.flash_bundle) as flash, \
                        patch.object(fast, "cleanup_device", wraps=fast.cleanup_device) as cleanup:
                    self.assertEqual(fast.main(), 0)
                    expected = 180 if timeout is None else timeout
                    self.assertEqual(flash.call_args.args[3].end, fast.STARTED + (expected - fast.CLEANUP_SECONDS))
                    self.assertEqual(cleanup.call_args.args[2].end, fast.STARTED + expected)
                self.assertTrue(device.closed)

    def run_boot(self, log):
        clock = [100.0]

        def read(count):
            clock[0] += 1
            return log

        device = fast.Device.__new__(fast.Device)
        device.serial = SimpleNamespace(read=read, reset_input_buffer=lambda: None)
        device.reset = lambda: None
        with patch.object(fast.time, "monotonic", side_effect=lambda: clock[0]):
            return device.boot(self.bundle, 0x10000, SimpleNamespace(end=130), self.folder)

    def test_runtime_boot_confirms_identity_and_exits_after_ready(self):
        log = b"Loaded app from partition at offset 0x10000\nApp version: 1.0.3\nELF file SHA256: abcdef123...\nUI ready on Home\n"
        self.assertTrue(all(self.run_boot(log).values()))

    def test_runtime_recovery_confirms_existing_firmware_without_new_identity(self):
        clock = [100.0]

        def read(count):
            clock[0] += 1
            return b"Loaded app from partition at offset 0x510000\nApp version: old\nUI ready on Home\n"

        device = fast.Device.__new__(fast.Device)
        device.reset = Mock()
        device.serial = SimpleNamespace(read=read)
        with patch.object(fast.time, "monotonic", side_effect=lambda: clock[0]):
            checks = device.recover(SimpleNamespace(end=110), self.folder)
        self.assertTrue(all(checks.values()))
        device.reset.assert_called_once()

    def test_runtime_boot_fatal_wrong_firmware_or_missing_ui_fail(self):
        for log in (b"Guru Meditation", b"Brownout", b"Rebooting", b"UI not ready",
                    b"Loaded app from partition at offset 0x510000\nUI ready on Home\n"):
            with self.subTest(log=log), self.assertRaises(fast.FlashError):
                self.run_boot(log)

    def test_cleanup_failure_is_not_reported_as_success(self):
        device = MemoryDevice(self.bundle)
        with self.main_mocks("flash", device), patch.object(device, "close", side_effect=OSError("close failed")):
            with self.assertRaises(fast.FlashError):
                fast.main()
        receipt = json.loads(next(self.folder.glob("*/result.json")).read_text())
        self.assertEqual(receipt["status"], "failed")
        self.assertFalse(receipt["serial_released"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
