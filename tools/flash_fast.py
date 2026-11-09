#!/usr/bin/env python3
"""中文：本仓库限时应用刷写；不负责整机恢复、发布或功能验收。
English: Bounded app-only flashing for this checkout, not recovery, release or acceptance.
"""

import argparse
import binascii
import contextlib
import hashlib
import importlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
STORE = ROOT / "build/flash-fast"
LIMIT = 180
CLEANUP_SECONDS = 10
SECTOR = 4096
STARTED = min(float(os.environ.get("_E4P_FLASH_STARTED", time.monotonic())), time.monotonic())


class FlashError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise FlashError(message)


def digest(data, algorithm="sha256"):
    return hashlib.new(algorithm, data).hexdigest()


def save_json(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n")
    temporary.replace(path)


class Budget:
    def __init__(self, seconds=LIMIT):
        self.end = STARTED + seconds

    def remaining(self):
        return max(0, self.end - time.monotonic())

    def need(self, seconds, message):
        require(self.remaining() >= seconds, message)


def run(command, budget, logfile=None, seconds=None, env=None):
    timeout = min(seconds or LIMIT, budget.remaining())
    require(timeout > 0, "Time budget exhausted; no automatic retry.")
    with contextlib.ExitStack() as stack:
        output = stack.enter_context(logfile.open("wb")) if logfile else subprocess.PIPE
        process = stack.enter_context(subprocess.Popen(command, cwd=ROOT, env=env, stdout=output,
                                                       stderr=subprocess.STDOUT, start_new_session=True))
        try:
            result, _ = process.communicate(timeout=timeout)
        except BaseException:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise
    require(process.returncode == 0, f"Command failed ({process.returncode}); see {logfile or shlex.join(command)}")
    return result or b""


def source_digest(budget):
    # 包含未跟踪的新产品文件，不把 Git HEAD 相同当作源码相同。
    # Include new untracked product files; equal HEAD does not imply equal sources.
    paths = run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--",
                 "main", "components", "managed_components", "CMakeLists.txt", "sdkconfig.defaults",
                 "sdkconfig.ci", "partitions*.csv", "dependencies.lock"], budget).split(b"\0")
    value = hashlib.sha256()
    for name in sorted(set(paths) - {b""}):
        path = ROOT / os.fsdecode(name)
        value.update(name + b"\0")
        value.update(path.read_bytes() if path.is_file() else b"<deleted>")
    return value.hexdigest()


def cache_values(build):
    values = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        match = re.match(r"([^:#/][^:=]*):[^=]+=(.*)", line)
        if match:
            values[match[1]] = match[2]
    return values


def select_build(args):
    config = json.loads((STORE / "config.json").read_text()) if (STORE / "config.json").exists() else {}
    require(not config or config.get("root") == str(ROOT), "Cached checkout differs; run prepare with --build-dir.")
    selected = args.build_dir or config.get("build_dir")
    require(selected, "No prepared build. Run: python3 tools/flash_fast.py prepare --build-dir <existing-product-build>")
    build = (ROOT / selected).resolve()
    require(build.is_relative_to(ROOT / "build"), "Use an ignored product build inside this checkout's build/.")
    require((build / "CMakeCache.txt").is_file(), "Build is not configured; prepare an ESP-IDF 6.1 / S31 product build first.")
    values = cache_values(build)
    require(Path(values.get("CMAKE_HOME_DIRECTORY", "")).resolve() == ROOT, "Build belongs to another checkout.")
    python = Path(values.get("PYTHON", ""))
    require(python.is_file(), "Build's ESP-IDF Python is missing; do not install tools during a flash request.")
    environment = python.parent.parent
    same_python = (Path(sys.prefix).resolve() == environment.resolve() if (environment / "pyvenv.cfg").is_file()
                   else os.path.samefile(sys.executable, python))
    if not same_python:
        env = dict(os.environ, _E4P_FLASH_STARTED=str(STARTED))
        os.execve(python, [str(python), str(Path(__file__).resolve()), *sys.argv[1:]], env)
    return build, config, values


def build_product(build, budget, output, quick):
    description = json.loads((build / "project_description.json").read_text())
    require(description.get("target") == "esp32s31", "Only ESP32-S31 builds are accepted.")
    require(Path(description["project_path"]).resolve() == ROOT, "Project description belongs to another checkout.")
    before = source_digest(budget)
    env = dict(os.environ, IDF_PATH=description["idf_path"], IDF_TARGET="esp32s31",
               IDF_PYTHON_ENV_PATH=sys.prefix, ESP_IDF_VERSION=description["git_revision"].lstrip("v"))
    env["PATH"] = os.pathsep.join([str(Path(description["c_compiler"]).parent),
                                   str(Path(sys.executable).parent), env.get("PATH", "")])
    command = [sys.executable, str(Path(description["idf_path"]) / "tools/idf.py"),
               "--preview", "-B", str(build), "build"]
    print("Incremental build (20s maximum)" if quick else "Preparing product build", flush=True)
    try:
        run(command, budget, output / "build.log", 20 if quick else None, env)
    except subprocess.TimeoutExpired as error:
        raise FlashError("Build exceeded its budget; device untouched. Run prepare separately; no automatic retry.") from error
    require(before == source_digest(budget), "Sources changed while building; device untouched.")
    return before


def load_bundle(build):
    from esptool.bin_image import LoadFirmwareImage
    from esptool.targets.esp32s31 import ESP32S31ROM

    desc = json.loads((build / "project_description.json").read_text())
    require(desc["target"] == "esp32s31" and Path(desc["project_path"]).resolve() == ROOT,
            "Wrong target or checkout.")
    settings = json.loads((build / "flasher_args.json").read_text())
    require(settings["extra_esptool_args"]["chip"] == "esp32s31", "Wrong generated flash target.")
    require(settings["flash_settings"] == {"flash_mode": "dio", "flash_size": "16MB", "flash_freq": "80m"},
            "Generated flash settings are not the S31 product settings.")
    config_path = Path(desc["config_file"])
    config = config_path.read_text()
    for item in ("CONFIG_IDF_TARGET=\"esp32s31\"", "CONFIG_ESPTOOLPY_FLASHFREQ_80M=y",
                 "CONFIG_SPIRAM_MODE_OCT=y", "CONFIG_SPIRAM_SPEED_200M=y"):
        require(item in config.splitlines(), f"Product config missing: {item}")
    app_path = build / settings["app"]["file"]
    image_bytes = app_path.read_bytes()
    image = LoadFirmwareImage("esp32s31", image_bytes)
    image.verify()
    require(image.chip_id == ESP32S31ROM.IMAGE_CHIP_ID, "Application chip ID differs.")
    require(image.calculate_checksum() == image.checksum and image.append_digest
            and image.stored_digest == image.calc_digest, "Application checksum or SHA-256 is invalid.")
    # esp_app_desc_t 的版本、工程名、IDF 与 ELF 摘要来自镜像，不猜测构建目录名。
    # Read esp_app_desc_t identity from the image, not the build directory name.
    header = image.segments[0].data
    require(struct.unpack_from("<I", header)[0] == 0xABCD5432, "Application descriptor missing.")
    version = header[16:48].split(b"\0", 1)[0].decode()
    project = header[48:80].split(b"\0", 1)[0].decode()
    idf_version = header[112:144].split(b"\0", 1)[0].decode()
    elf_sha = header[144:176].hex()
    require(version == desc["project_version"] and project == desc["project_name"], "Image identity differs from build.")
    require(re.match(r"v?6\.1(?:[.-]|$)", idf_version), "ESP-IDF 6.1 is required.")
    require(digest((build / desc["app_elf"]).read_bytes()) == elf_sha, "ELF does not match the application.")
    sys.path.insert(0, str(Path(desc["idf_path"]) / "components/partition_table"))
    partition_module = importlib.import_module("gen_esp32part")
    table_bytes = (build / settings["partition-table"]["file"]).read_bytes()
    table = partition_module.PartitionTable.from_binary(table_bytes)
    table.verify()
    slots = sorted([p for p in table if p.type == 0 and 0x10 <= p.subtype < 0x20], key=lambda p: p.subtype)
    require([p.subtype for p in slots] == [0x10, 0x11], "Expected two consecutive OTA application slots.")
    require(int(settings["app"]["offset"], 0) == slots[0].offset, "Generated application offset differs from table.")
    otadata = next((p for p in table if p.type == 1 and p.subtype == 0), None)
    require(otadata and otadata.size == 2 * SECTOR, "Invalid OTA selection partition.")
    padded = image_bytes + b"\xff" * (-len(image_bytes) % SECTOR)
    require(all(len(padded) <= p.size and p.offset % SECTOR == 0 for p in slots), "Application exceeds an OTA slot.")
    boot = settings["bootloader"]
    return {"build_dir": str(build), "version": version, "image": image_bytes, "padded": padded,
            "image_sha256": digest(image_bytes), "elf_sha256": elf_sha, "settings": settings["flash_settings"],
            "table_offset": int(settings["partition-table"]["offset"], 0), "table": table_bytes,
            "boot_offset": int(boot["offset"], 0), "boot": (build / boot["file"]).read_bytes(),
            "slots": slots, "otadata": otadata, "config_sha256": digest(config_path.read_bytes())}


def boot_signature(data):
    from esptool.bin_image import LoadFirmwareImage
    from esptool.targets.esp32s31 import ESP32S31ROM

    image = LoadFirmwareImage("esp32s31", data)
    image.verify()
    require(image.chip_id == ESP32S31ROM.IMAGE_CHIP_ID and image.append_digest
            and image.checksum == image.calculate_checksum() and image.stored_digest == image.calc_digest,
            "Bootloader image integrity or chip check failed.")
    header = image.segments[0].data
    magic, version, idf = struct.unpack_from("<B3xI32s", header)
    require(magic == 0x50, "Bootloader descriptor missing.")
    idf = idf.split(b"\0", 1)[0].decode()
    require(re.match(r"v?6\.1(?:[.-]|$)", idf), "Existing bootloader is not ESP-IDF 6.1.")
    return {"chip": image.chip_id, "mode": image.flash_mode, "size_freq": image.flash_size_freq,
            "bootloader_version": version, "idf_series": "6.1"}


def active_slot(data, slots):
    require(len(data) == 2 * SECTOR, "Short OTA selection read.")
    records = []
    for offset in (0, SECTOR):
        sequence, state, crc = struct.unpack_from("<I20xII", data, offset)
        expected = binascii.crc32(struct.pack("<I", sequence), 0xFFFFFFFF) & 0xFFFFFFFF
        if sequence not in (0, 0xFFFFFFFF) and state not in (3, 4) and crc == expected:
            records.append((sequence, state))
    require(records, "No validated OTA selection; use an explicitly authorized recovery workflow.")
    selected = max(records, key=lambda record: record[0])
    require(selected[1] == 2, "Selected OTA image is not VALID; do not overwrite a pending/rollback image.")
    return slots[(selected[0] - 1) % len(slots)]


def changed_spans(old, new):
    require(new and len(new) % SECTOR == 0, "Write data must cover complete sectors.")
    if old is None:
        return [(0, new)]
    spans = []
    for offset in range(0, len(new), SECTOR):
        if old[offset:offset + SECTOR] == new[offset:offset + SECTOR]:
            continue
        if spans and spans[-1][0] + len(spans[-1][1]) == offset:
            start, previous = spans[-1]
            spans[-1] = (start, previous + new[offset:offset + SECTOR])
        else:
            spans.append((offset, new[offset:offset + SECTOR]))
    if len(spans) > 24 or sum(len(data) for _, data in spans) > len(new) // 2:
        return [(0, new)]
    return spans


def normalize_mac(value):
    compact = re.sub(r"[:-]", "", value or "").lower()
    require(bool(re.fullmatch(r"[0-9a-f]{12}", compact)), "A USB serial / --expect-mac with a six-byte MAC is required.")
    return ":".join(compact[index:index + 2] for index in range(0, 12, 2))


def select_port(requested, expected):
    from serial.tools import list_ports

    ports = [p for p in list_ports.comports() if p.vid == 0x303A and p.pid == 0x1001]
    if requested:
        ports = [p for p in ports if p.device == requested]
    if expected:
        ports = [p for p in ports if re.sub(r"[:-]", "", p.serial_number or "").lower() == expected.replace(":", "")]
    require(len(ports) == 1, "Expected one matching native ESP USB port; connect it or specify --port / --expect-mac.")
    port = ports[0]
    return port.device, expected or normalize_mac(port.serial_number)


class Device:
    def __init__(self, port):
        import serial
        from esptool import cmds
        from esptool.targets.esp32s31 import ESP32S31ROM

        self.cmds = cmds
        self.serial = serial.Serial(port=None, baudrate=115200, timeout=3, exclusive=True)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        self.esp = ESP32S31ROM(self.serial, baud=115200)
        self.opened = False
        self.boot_attempted = False

    def connect(self):
        self.serial.open()
        self.opened = True
        self.esp.connect(attempts=1)
        info = self.esp.get_security_info()
        require(info["chip_id"] == self.esp.IMAGE_CHIP_ID, "Connected chip is not ESP32-S31.")
        require(not info["parsed_flags"]["SECURE_BOOT_EN"] and
                not self.esp.get_flash_encryption_enabled() and not self.esp.secure_download_mode,
                "Secure/encrypted devices require a separate workflow.")
        self.mac = ":".join(f"{byte:02x}" for byte in self.esp.read_mac())
        # S31 ROM 连续读取已实测中断；使用官方 RAM stub，不改设备 bootloader。
        # Repeated S31 ROM reads failed on hardware; use the official RAM stub without changing flash boot code.
        self.esp = self.cmds.run_stub(self.esp)
        require(self.esp.IS_STUB, "Official S31 RAM stub unavailable; no ROM fallback.")
        self.esp.change_baud(460800)
        self.cmds.attach_flash(self.esp)
        require(self.cmds.detect_flash_size(self.esp) == "16MB", "Connected flash is not 16MB.")

    def read(self, offset, size):
        return self.cmds.read_flash(self.esp, offset, size, flash_size="16MB", no_progress=True)

    def md5(self, offset, size):
        return self.esp.flash_md5sum(offset, size)

    def write(self, pairs, settings):
        self.cmds.write_flash(self.esp, pairs, no_compress=True, no_progress=True, **settings)

    def check_bootloader(self, bundle):
        span = bundle["table_offset"] - bundle["boot_offset"]
        require(0 < span <= 0x10000, "Generated bootloader region is invalid.")
        stored = self.read(bundle["boot_offset"], span)
        expected = boot_signature(bundle["boot"])
        require(boot_signature(stored) == expected,
                "Bootloader base compatibility differs; no automatic bootloader update.")
        return {"signature": expected, "matches_build": stored[:len(bundle["boot"])] == bundle["boot"]}

    def boot(self, bundle, offset, budget, output):
        return self.observe_boot(bundle, offset, budget, output / "boot.log", 20)

    def recover(self, budget, output):
        return self.observe_boot(None, None, budget, output / "recovery-boot.log", 8)

    def reset(self):
        from esptool.reset import HardReset

        self.boot_attempted = True
        self.serial.baudrate = 115200
        self.serial.timeout = 0.1
        self.serial.reset_input_buffer()
        HardReset(self.serial, uses_usb=True)()

    def observe_boot(self, bundle, offset, budget, logfile, seconds):
        self.reset()
        end = min(time.monotonic() + seconds, budget.end - 0.5)
        captured = bytearray()
        ready_at = None
        with logfile.open("wb") as log:
            while time.monotonic() < end:
                data = self.serial.read(8192)
                captured.extend(data)
                log.write(data)
                text = captured.decode("utf-8", errors="replace")
                require(not re.search(r"Guru Meditation|panic|abort\(\)|Brownout|Rebooting", text),
                        f"Boot reported a fatal error; see {logfile.name}. No automatic rollback.")
                if bundle is not None:
                    checks = boot_checks(text, bundle, offset)
                else:
                    checks = {"application": "Loaded app from partition at offset" in text,
                              "version": bool(re.search(r"App version:\s*\S+", text)),
                              "ui_ready": "UI ready on" in text}
                if all(checks.values()):
                    ready_at = ready_at or time.monotonic()
                    if time.monotonic() - ready_at >= 1:
                        return checks
        raise FlashError(f"Application/UI boot was not confirmed within {seconds}s; see {logfile.name}.")

    def close(self):
        self.serial.close()
        self.opened = False


def cleanup_device(device, receipt, budget, output):
    try:
        if device and device.opened and receipt["status"] != "success":
            if receipt["write_started"] and not receipt["flash_verified"]:
                receipt["recovery"] = {"status": "required", "reason": "Interrupted application write; not booted or retried."}
            elif device.boot_attempted:
                receipt["recovery"] = {"status": "unconfirmed", "reason": "Boot already attempted; no reset loop."}
            else:
                receipt["recovery"] = {"status": "running"}
                save_json(output / "result.json", receipt)
                print("No incomplete write; restoring application boot (8s maximum)", flush=True)
                try:
                    checks = device.recover(budget, output)
                    receipt["recovery"] = {"status": "success", "boot": checks}
                except Exception as error:
                    receipt["recovery"] = {"status": "failed", "error": str(error)}
    finally:
        if device:
            receipt["device_opened"] = receipt["device_opened"] or device.opened
            device.close()
        receipt["serial_released"] = True


def boot_checks(text, bundle, offset):
    matches = re.findall(r"Loaded app from partition at offset (0x[0-9a-fA-F]+)", text)
    versions = re.findall(r"App version:\s*(\S+)", text)
    hashes = re.findall(r"ELF file SHA256:\s*([0-9a-fA-F]+)", text)
    return {"slot": bool(matches) and int(matches[-1], 16) == offset,
            "version": bool(versions) and versions[-1] == bundle["version"],
            "elf": bool(hashes) and len(hashes[-1]) >= 8 and bundle["elf_sha256"].startswith(hashes[-1].lower()),
            "ui_ready": "UI ready on" in text}


def flash_bundle(bundle, device, expected_mac, budget, output, receipt, rewrite_app=False):
    require(device.mac == expected_mac, "Connected MAC differs; nothing written.")
    require(device.read(bundle["table_offset"], len(bundle["table"])) == bundle["table"],
            "Partition table differs; no automatic full-device flash.")
    receipt["bootloader"] = device.check_bootloader(bundle)
    selection = device.read(bundle["otadata"].offset, bundle["otadata"].size)
    slot = active_slot(selection, bundle["slots"])
    baseline = STORE / f"{expected_mac.replace(':', '')}-{slot.offset:x}.bin"
    image = bundle["padded"]
    old = None
    if baseline.is_file():
        candidate = baseline.read_bytes()
        if candidate and len(candidate) <= slot.size and len(candidate) % SECTOR == 0:
            if device.md5(slot.offset, len(candidate)) == digest(candidate, "md5"):
                old = candidate
    if rewrite_app:
        spans = changed_spans(None, image)
    elif device.md5(slot.offset, len(image)) == digest(image, "md5"):
        spans = []
    else:
        spans = changed_spans(old, image)
    # 官方 stub 已实测约 170KB/s，按 100KB/s 保守估算并保留启动余量。
    # The official stub measured about 170KB/s; budget at 100KB/s and reserve boot time.
    transfer = sum(len(data) / 100000 + 0.3 for _, data in spans)
    budget.need(25 + transfer, "Insufficient time to safely start writing; device not written. Prepare/reconnect and request a new run.")
    receipt.update(mac=device.mac, slot=slot.name, offset=slot.offset,
                   write_bytes=sum(len(data) for _, data in spans), write_spans=len(spans),
                   baseline_verified=old is not None, write_started=False,
                   write_ranges=[{"offset": slot.offset + offset, "bytes": len(data), "sha256": digest(data)}
                                 for offset, data in spans], flash_settings=bundle["settings"])
    save_json(output / "result.json", receipt)
    if spans:
        print(f"Write {slot.name} @ {slot.offset:#x}: {receipt['write_bytes']} bytes / {len(spans)} span(s)", flush=True)
        receipt["write_started"] = True
        save_json(output / "result.json", receipt)
        pairs = [(slot.offset + offset, data) for offset, data in spans]
        require(all(slot.offset <= address and address + len(data) <= slot.offset + slot.size
                    for address, data in pairs), "Write exceeds selected application slot.")
        device.write(pairs, bundle["settings"])
    require(device.md5(slot.offset, len(image)) == digest(image, "md5"), "Full application digest verification failed.")
    require(device.read(bundle["otadata"].offset, len(selection)) == selection, "OTA selection unexpectedly changed.")
    receipt["flash_verified"] = True
    save_json(output / "result.json", receipt)
    print("Application digest verified; confirming boot (20s maximum)", flush=True)
    receipt["boot"] = device.boot(bundle, slot.offset, budget, output)
    baseline.write_bytes(image)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("status", "prepare", "flash"), nargs="?", default="status")
    parser.add_argument("--build-dir", help="Existing configured product build under build/")
    parser.add_argument("--port", help="Native USB port; never guessed when ambiguous")
    parser.add_argument("--expect-mac", help="Required device identity, otherwise use native USB serial")
    parser.add_argument("--yes", action="store_true", help="Confirm a current user-authorized app-only flash")
    parser.add_argument("--rewrite-app", action="store_true", help="Explicitly rewrite the selected app even if already installed")
    parser.add_argument("--timeout", type=int, default=LIMIT, choices=range(1, LIMIT + 1), metavar=f"1..{LIMIT}")
    args = parser.parse_args()
    require(args.mode != "flash" or args.yes, "Flashing requires current user authorization and --yes.")
    require(not args.rewrite_app or args.mode == "flash", "--rewrite-app is only valid with flash.")
    budget = Budget(args.timeout)
    work_budget = Budget(max(0, args.timeout - CLEANUP_SECONDS)) if args.mode == "flash" else budget

    def expired(signum, frame):
        raise FlashError("Operation time budget reached; stopping work and finishing bounded cleanup. No retry.")

    signal.signal(signal.SIGALRM, expired)
    signal.setitimer(signal.ITIMER_REAL, max(0.001, work_budget.remaining()))
    signal.signal(signal.SIGTERM, expired)
    os.umask(0o077)
    build, config, _ = select_build(args)
    STORE.mkdir(parents=True, exist_ok=True, mode=0o700)
    STORE.chmod(0o700)
    lock = (STORE / "operation.lock").open("a")
    import fcntl
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError as error:
        raise FlashError("Another fast-flash operation is running; no retry or forced process termination.") from error
    with lock:
        expected = args.expect_mac or config.get("expect_mac")
        expected = normalize_mac(expected) if expected else None
        if args.mode == "status":
            bundle = load_bundle(build)
            print(json.dumps({"build_dir": str(build), "version": bundle["version"],
                              "image_sha256": bundle["image_sha256"], "expect_mac": expected,
                              "device_opened": False, "flash_limit_seconds": LIMIT}, indent=2))
            return 0
        output = STORE / f"{time.strftime('%Y%m%d-%H%M%S')}-{os.getpid()}"
        output.mkdir(mode=0o700)
        receipt = {"status": "running", "build_dir": str(build), "device_opened": False,
                   "command": shlex.join([sys.executable, *sys.argv]),
                   "write_started": False, "flash_verified": False}
        device = None
        try:
            source = build_product(build, work_budget, output, quick=args.mode == "flash")
            bundle = load_bundle(build)
            receipt.update(version=bundle["version"], image_sha256=bundle["image_sha256"],
                           image_bytes=len(bundle["image"]), elf_sha256=bundle["elf_sha256"], source_sha256=source)
            require(source == source_digest(work_budget), "Sources changed during validation; device untouched.")
            save_json(STORE / "config.json", {"root": str(ROOT), "build_dir": str(build), "expect_mac": expected})
            if args.mode == "flash":
                port, expected = select_port(args.port, expected)
                work_budget.need(40, "Not enough time for device preflight; device untouched.")
                lsof = shutil.which("lsof")
                if lsof:
                    busy = subprocess.run([lsof, "-t", port], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=2)
                    require(not busy.stdout.strip(), f"Serial port is occupied: {port}; no processes were stopped.")
                receipt.update(port=port)
                save_json(output / "result.json", receipt)
                print(f"Connect S31 {expected} on {port}; app-only, no full backup or erase", flush=True)
                device = Device(port)
                device.connect()
                receipt["device_opened"] = True
                receipt["transport"] = "official-s31-ram-stub"
                flash_bundle(bundle, device, expected, work_budget, output, receipt, args.rewrite_app)
            receipt["status"] = "success"
        except BaseException as error:
            receipt.update(status="failed", error=str(error))
            raise
        finally:
            signal.setitimer(signal.ITIMER_REAL, max(0.001, budget.remaining()))
            cleanup_error = None
            try:
                cleanup_device(device, receipt, budget, output)
            except Exception as error:
                cleanup_error = error
                receipt.update(status="failed", serial_released=False, cleanup_error=str(error))
            receipt["seconds"] = round(time.monotonic() - STARTED, 2)
            save_json(output / "result.json", receipt)
            print(f"Result: {output / 'result.json'}", flush=True)
            signal.setitimer(signal.ITIMER_REAL, 0)
            if cleanup_error and sys.exc_info()[0] is None:
                raise FlashError(f"Serial cleanup failed: {cleanup_error}")
        print(f"{args.mode}: success in {receipt['seconds']}s; version {bundle['version']}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"STOP: {error}", file=sys.stderr)
        sys.exit(1)
