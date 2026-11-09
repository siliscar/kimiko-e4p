---
name: kimiko-e4p-flash
description: 仅为本仓库 Metalio E-INK4 Plus / ESP32-S31 执行本地烧录、刷机、烧写最新代码或重新刷入固件，使用限时应用分区脚本。创建脚本或 skill 不授权刷机；不用于发布、联网 OTA、整机恢复或其他仓库。
---

# E4P 快速刷机

仅在当前 `kimiko-e4p` checkout 使用。入口为 `tools/flash_fast.py`，禁止安装到用户级 skills 或复制到其他仓库。
Use only this checkout's helper; never install this skill globally or apply it to another repository.

## 时限 / Deadline

- 用户 2026-11-05 要求放宽脚本时限，取代此前两分钟总预算。脚本默认及上限改为 **180 秒**，其中末尾 **10 秒**留给失败退出和设备启动恢复；可用 `--timeout 1..180` 缩短本次时限。立即执行入口，不先跑索引、全量测试、全片备份或重读历史。普通刷机不修改生产函数，因此不需要为这次硬件操作重新做代码编辑的图分析。
  The user's 2026-11-05 request supersedes the two-minute total budget. The helper now defaults to a 180-second maximum, reserving its last ten seconds for safe cleanup and boot restoration; `--timeout 1..180` can shorten a run. Do not add unrelated audits to flash-only work.
- “最新代码”指当前 checkout，包含未提交修改；不擅自拉远端、切分支、递增版本或重写已发布文件。
  Latest means the current checkout, including uncommitted changes, without an implicit pull, branch switch or version change.
- 这是时限要求，不是保证冷构建、断连设备也能刷成。缺少构建、20 秒增量编译失败、端口异常、分区/bootloader 不兼容或预算不足时，立即报告失败，不换旧 Bin，不自动重试，不延长时限。需要完整构建/整机恢复时，单独说明并取得用户对超时准备或恢复的授权。
  Cold builds, connection failures and incompatible layouts must fail promptly; never substitute stale firmware, retry indefinitely or widen authorization to meet a time target.

## 执行 / Execute

用户本轮明确要求刷机才可使用 `flash --yes`。请求设计/修改工具、查看状态或旧聊天许可均不算授权；不要为测试 skill 自动访问串口。
Only a current explicit flash request authorizes `flash --yes`; tool creation, status checks and historical permissions do not.

用户明确要求恢复当前异常设备、调试刷机故障时，可针对该设备验证只读检查、失败复位及当前应用刷写；这不授权改刷其他固件、整机恢复或反复试刷。需要验证实际写入路径时用一次 `flash --yes --rewrite-app`，仍受同一 180 秒限制。
An explicit request to recover this device or debug its failed flash permits bounded read/reset and current-app validation, not unrelated firmware or whole-device recovery. One `flash --yes --rewrite-app` run can validate real writes under the same deadline.

已有本机准备记录时只执行一次：
With a prepared local build, execute once:

```sh
python3 tools/flash_fast.py flash --yes
```

明确指定现有构建或设备时可加 `--build-dir build/<product>`、`--port /dev/cu.usbmodem...`、`--expect-mac xx:xx:xx:xx:xx:xx`；从该次构建生成信息自动选择 IDF Python，不反复激活/寻找环境。无准备记录时不要扫描全部历史构建，使用本次开发已经验证的产品构建；无法确定则立即询问，不猜测。
Optional arguments select an explicitly known product build or device. The helper selects the build's IDF Python; do not scan historical builds or guess a candidate.

执行工具使用短等待取得运行会话，随后按剩余时间等待完成；不要把整段脚本重复粘到命令中，不每几秒重复轮询。到时如失败，读取输出指向的单个 `result.json`，区分未写入、部分写入、写入校验通过但未确认启动；不宣称超时失败为成功。
Use a short initial tool yield and a bounded completion wait. Inspect the single reported receipt after failure; distinguish untouched, interrupted write and verified-flash/unconfirmed-boot states.

失败时必须同时读取 `recovery` 和 `serial_released`。未写入或已完整校验、且尚未启动的设备，由脚本在原预算内执行一次 USB 复位并观察首页就绪；这是退出清理，不是重刷。不得将“串口已关闭”当作“设备已恢复”。部分写入未校验则不启动未知应用，明确报告需恢复；已尝试启动仍失败则不循环复位。恢复日志为同一结果目录的 `recovery-boot.log`。
After failure, inspect both recovery and port-release results. Safe, unbooted devices get one bounded USB reset with UI confirmation; closing the port alone is not recovery. Incomplete writes remain in recovery mode, and failed boots are not reset repeatedly.

## 提前准备 / Prepare Ahead

在功能开发的构建验证阶段（用户尚未要求刷机时），将已知产品构建登记并保持增量构建可用：
During the normal build-validation phase, register the known product build without touching hardware:

```sh
python3 tools/flash_fast.py prepare --build-dir build/<product>
python3 tools/flash_fast.py status
```

`prepare` 不开串口，也不授予后续刷机许可。配置、基线与记录仅在被忽略的 `build/flash-fast/`；不要提交、上传或覆盖已有备份。不要将发布包、临时诊断构建或别的 worktree 当作当前产品构建。
Preparation opens no serial port and grants no later permission. Keep configuration, baselines and receipts ignored and local; never select release bundles, diagnostic builds or another checkout implicitly.

## 保护与结果 / Protection And Result

脚本核对芯片/MAC/安全状态/16MB，使用官方 esptool S31 **RAM stub** 完成同一连接内的读取与写入；不回退到已复现读取中断的 ROM 路径，stub 不写设备 bootloader。核对生成分区表与 bootloader，实读 otadata 选择 VALID 活动槽；仅写该应用分区，不改 bootloader、分区表、otadata、备用应用、NVS、storage 或 TF。首次/基线不匹配时整写**活动应用**，不是整机；只有设备摘要匹配本机基线时才按完整 4KB 扇区增量写。固件已一致时不重复写入。始终验证完整应用摘要，最多观察 20 秒以确认槽位、版本、ELF 和 `UI ready`，随后关闭串口。
Use the official S31 RAM stub, not the failing ROM read path; it does not replace the device bootloader. Validate identity/layout, derive the active VALID OTA slot, write only that app and verify its complete digest. Deltas need a device-verified baseline; identical firmware skips writes. Confirm boot within 20 seconds, then close the port.

最终简短给出成功/失败、实际耗时和版本；已一致而跳过写入时如实说明，有失败时说明写入和恢复状态。启动通过不等于显示、天气或阅读功能验收；用户说刷完停止，就不再自动操作页面或继续试验。
Report outcome, duration and version; disclose skipped writes and failed write/recovery state. Boot confirmation is not optical or feature acceptance. Stop after flashing when requested.
