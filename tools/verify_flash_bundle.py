#!/usr/bin/env python3
"""Validate the public mogo web-flashing bundle."""

from __future__ import annotations

import hashlib
import json
import sys
import struct
import re
from pathlib import Path


# 本仓库对应的设备与布局：Mogo E4P（ESP32-S31，16 MB，5 MB 双槽）。
# Device and layout for this repository: Mogo E4P (ESP32-S31, 16 MB, dual 5 MB slots).
# 打包与校验共用这一份，布局变化只改这里；校验仍按显式期望值卡住意外布局。
# Packaging and validation share this block, so a layout change edits one place; validation stays
# an explicit expectation gate rather than trusting whatever was built.
LAYOUT = {
    "board": "METALIO-EINK4P",
    "layout": "mogo-e4p-dual-5m-v1",
    "project": "Read_Pico",
    "chip_id": 0x20,
    "chip_family": "ESP32-S31",
    "minimum_base_version": "0.3.3-rc97",
    "online_ota_base_version": "0.3.3-rc97",
}

# 每项是 (烧录偏移, 下一区域起点)，差值即该部件的最大字节数。
# Each entry is (flash offset, start of the next region); the difference is the part's size cap.
EXPECTED = {
    "bootloader.bin": (0x2000, 0x8000),
    "partitions.bin": (0x8000, 0x9000),
    "firmware.bin": (0x10000, 0x510000),
    "ota_data_initial.bin": (0xD000, 0xF000),
}

# 分区布局只有两个已登记时代：OTA 槽位与 otadata 永不移动（升级契约）；nvs/storage 于 2026-10
# 从头部搬到 flash 末尾，完整刷机不再覆盖用户数据。新构建必须逐项等于当前时代。
# Partition layouts have exactly two registered eras: OTA slots and otadata never move (upgrade
# contract); nvs/storage moved from the head to end of flash in 2026-10 so full-image flashes stop
# wiping user data. New builds must match the current era entry by entry.
LAYOUT_2026_10_TAIL_NVS = {
    "otadata": (1, 0, 0xD000, 0x2000),
    "phy_init": (1, 1, 0xF000, 0x1000),
    "ota_0": (0, 16, 0x10000, 0x500000),
    "ota_1": (0, 17, 0x510000, 0x500000),
    "storage": (1, 129, 0xA10000, 0x5E8000),
    "nvs": (1, 2, 0xFF8000, 0x8000),
}
LAYOUT_LEGACY_HEAD_NVS = {
    "otadata": (1, 0, 0xD000, 0x2000),
    "phy_init": (1, 1, 0xF000, 0x1000),
    "ota_0": (0, 16, 0x10000, 0x500000),
    "ota_1": (0, 17, 0x510000, 0x500000),
    "storage": (1, 129, 0xA10000, 0x5F0000),
    "nvs": (1, 2, 0x9000, 0x4000),
}


def parse_partition_table(data: bytes) -> dict[str, tuple[int, int, int, int]]:
    entries: dict[str, tuple[int, int, int, int]] = {}
    for offset in range(0, len(data) - 31, 32):
        magic, kind, subtype, address, size, label, _ = struct.unpack_from("<HBBII16sI", data, offset)
        if magic != 0x50aa: break
        entries[label.split(b"\0")[0].decode()] = (kind, subtype, address, size)
    return entries

# 发布仓库的 Pages 站点；打包写清单与校验清单共用这一份，避免两处漂移。
# Pages origin of the release repository, shared by the feed writer and the validator so they cannot drift.
PAGES_ORIGIN = "https://chopper1026.github.io/mogo-e4p/"


def check_archived_releases(folder: Path) -> list[dict]:
    # 历史版本只引用原正式固件，并核对每个完整刷机部件的大小、地址与校验值。
    # Historical choices use original releases; verify every base part's size, address and hash.
    catalog_path = folder / "releases.json"
    if not catalog_path.is_file():
        return []
    catalog = json.loads(catalog_path.read_text())
    assert catalog.get("schema") == 1 and isinstance(catalog.get("releases"), list)
    versions = set()
    for release in catalog["releases"]:
        version = release["version"]
        assert re.fullmatch(r"\d+\.\d+\.\d+(?:-rc\d+)?", version)
        assert version not in versions, "duplicate archived version"
        versions.add(version)
        assert re.fullmatch(r"[0-9a-f]{40}", release["source_commit"]), "missing release provenance"
        assert release["manifest"] == f"releases/{version}/manifest.json"
        assert release["bin"] == f"Pico-update-{version}.bin"
        manifest_path = folder / release["manifest"]
        manifest = json.loads(manifest_path.read_text())
        assert manifest["version"] == version and manifest.get("new_install_prompt_erase") is True
        assert manifest["pico_ota"]["base_version"] == version
        assert manifest["pico_ota"]["layout"] == LAYOUT["layout"] and manifest["pico_ota"]["rollback"] is True
        builds = manifest["builds"]
        assert len(builds) == 1 and builds[0]["chipFamily"] == LAYOUT["chip_family"]
        paths = {name: name for name in EXPECTED}
        paths["firmware.bin"] = f"../../{release['bin']}"
        parts = builds[0]["parts"]
        assert len(parts) == len(EXPECTED) and set(release["files"]) == set(EXPECTED)
        seen = set()
        for part in parts:
            name = next((name for name, path in paths.items() if part["path"] == path), None)
            assert name and name not in seen, "unexpected historical part"
            seen.add(name)
            start, end = EXPECTED[name]
            assert part["offset"] == start, "wrong historical flash address"
            target = (manifest_path.parent / part["path"]).resolve()
            assert target.is_relative_to(folder.resolve()), "historical file escapes public bundle"
            data = target.read_bytes()
            assert data and len(data) <= end - start
            expected = release["files"][name]
            assert expected["size"] == len(data) and expected["sha256"] == hashlib.sha256(data).hexdigest(), (
                f"historical bytes changed: {version} {name}")
            if name == "firmware.bin":
                assert data[0] == 0xe9 and struct.unpack_from("<H", data, 12)[0] == LAYOUT["chip_id"]
                assert struct.unpack_from("<I", data, 32)[0] == 0xabcd5432
                assert data[48:80].split(b"\0")[0].decode() == version
                assert data[80:112].split(b"\0")[0].decode() == LAYOUT["project"]
                assert b"PICO_HTTPS_OTA_V1" in data
            elif name == "partitions.bin":
                # 历史版本整表必须等于某个已登记时代，禁止任何未登记的布局漂移。
                # Historical tables must equal a registered era exactly; no unregistered layout drift.
                assert parse_partition_table(data) in (LAYOUT_LEGACY_HEAD_NVS, LAYOUT_2026_10_TAIL_NVS), (
                    f"historical layout incompatible: {version}")
        print(f"Historical USB bundle verified: {version} (original bytes, preserve-data default)")
    assert (folder / "versions.js").is_file(), "missing version selector"
    return catalog["releases"]


def check(folder: Path) -> None:
    manifest = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    assert manifest.get("new_install_prompt_erase") is True, "flasher must preserve user data"
    builds = manifest.get("builds")
    assert isinstance(builds, list) and len(builds) == 1
    assert builds[0].get("chipFamily") == LAYOUT["chip_family"]
    parts = builds[0].get("parts")
    assert isinstance(parts, list) and len(parts) == len(EXPECTED)
    seen: set[str] = set()
    for part in parts:
        name = part.get("path")
        assert name in EXPECTED and name not in seen, f"unexpected flash part: {name}"
        seen.add(name)
        start, end = EXPECTED[name]
        assert part.get("offset") == start, f"wrong offset for {name}"
        data = (folder / name).read_bytes()
        assert data and len(data) <= end - start, f"invalid size for {name}"
        print(f"{name}: {len(data)} bytes, SHA256 {hashlib.sha256(data).hexdigest()}")
    assert seen == set(EXPECTED)
    # 升级兼容性门禁：实际双槽分区、应用描述、基础包能力及升级 SHA 必须一致。
    # Upgrade gate: real slots, app description, base capability and upgrade SHA must agree.
    entries = parse_partition_table((folder / "partitions.bin").read_bytes())
    assert entries.get("ota_0") == LAYOUT_2026_10_TAIL_NVS["ota_0"], "base OTA slot changed"
    assert entries.get("ota_1") == LAYOUT_2026_10_TAIL_NVS["ota_1"], "inactive OTA slot changed"
    assert entries.get("otadata") == LAYOUT_2026_10_TAIL_NVS["otadata"], "OTA selection metadata changed"
    assert entries.get("nvs") == LAYOUT_2026_10_TAIL_NVS["nvs"], "user settings partition changed"
    assert entries.get("storage") == LAYOUT_2026_10_TAIL_NVS["storage"], "user storage partition changed"
    image = (folder / "firmware.bin").read_bytes()
    assert image[0] == 0xe9 and struct.unpack_from("<H", image, 12)[0] == LAYOUT["chip_id"], "wrong target chip"
    assert struct.unpack_from("<I", image, 32)[0] == 0xabcd5432, "missing app description"
    version = image[48:80].split(b"\0")[0].decode("ascii")
    project = image[80:112].split(b"\0")[0].decode("ascii")
    assert version == manifest["version"] and project == LAYOUT["project"], "base version mismatch"
    assert re.fullmatch(r"\d+\.\d+\.\d+(?:-rc\d+)?", version), "invalid release version"
    capability = manifest.get("pico_ota", {})
    assert capability.get("layout") == LAYOUT["layout"]
    assert capability.get("base_version") == version and capability.get("rollback") is True
    assert capability.get("minimum_base_version") == LAYOUT["minimum_base_version"]
    assert capability.get("online_ota_base_version") == LAYOUT["online_ota_base_version"]
    assert b"PICO_HTTPS_OTA_V1" in image, "base package cannot perform online upgrades"
    feed = json.loads((folder / "update.json").read_text())
    upgrade_name = f"Pico-update-{version}.bin"
    assert feed["schema"] == 1 and feed["version"] == version and feed["project"] == project
    assert feed["board"] == LAYOUT["board"] and feed["layout"] == capability["layout"]
    assert feed["minimum_base_version"] == capability["minimum_base_version"]
    assert feed["url"] in (f"{PAGES_ORIGIN}{upgrade_name}", f"https://kiikoread.com/{upgrade_name}")
    assert feed["size"] == len(image) and feed["sha256"] == hashlib.sha256(image).hexdigest()
    assert (folder / "Pico-update.bin").read_bytes() == image
    assert (folder / upgrade_name).read_bytes() == image
    print(f"OTA base/TF/feed compatible: {version}, {capability['layout']}")
    # 只有完整站点目录才核对网页资源；仅打包固件时只验固件与清单。
    # Check web assets only in a full site tree; bundle-only staging validates firmware and feed.
    if (folder / "index.html").is_file():
        assert (folder / "vendor/web/install-button.js").is_file()
        assert (folder / "licenses/LUCIDE-ISC.txt").is_file()
        check_archived_releases(folder)


if __name__ == "__main__":
    check(Path(sys.argv[1]) if len(sys.argv) > 1 else Path("flash"))
