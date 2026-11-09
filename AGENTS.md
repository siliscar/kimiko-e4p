# AGENTS.md

面向 AI agent 的仓库入口。人类也可读。产品概述、硬件表、引脚、编译命令见 [README.md](README.md)。

Agent entry for this tree. Humans can read it too. Product overview, hardware, pinout and flash commands live in [README.md](README.md).

新人（人或 agent）上手看 [docs/ONBOARDING.md](docs/ONBOARDING.md)；接手进行中的任务先看 [docs/HANDOFF.md](docs/HANDOFF.md)。

Newcomers (human or agent): read [docs/ONBOARDING.md](docs/ONBOARDING.md). Taking over an in-flight task: read [docs/HANDOFF.md](docs/HANDOFF.md) first.

## 本移植仓库 / This port

当前硬件为 Metalio E-INK4 Plus（ESP32-S31）；构建及硬件入口以根目录 [README.md](README.md) 为准。下文 S3 / CW32 / SY7636A 内容是上游背景；不要据此将产品目标切回 S3。现用 `board_*`、`cst836u_*`、`fca9555_*` 名称包含新板兼容接口，不能仅凭旧芯片名删除。
This port targets Metalio E-INK4 Plus (ESP32-S31); use the root [README.md](README.md) for its build and hardware entry points. S3 / CW32 / SY7636A details below describe upstream history, not the product target. Existing `board_*`, `cst836u_*` and `fca9555_*` names include active compatibility interfaces for this board; a legacy chip name does not imply dead code.

原厂仓库本地路径：`~/codespace/Metalio-E-INK4-Plus`（`~` 代表当前用户的主目录）。今后提到「原厂」均指此仓库；其固件含 SC7A20H 驱动与 AOI1 运动中断等硬件基础，可作硬件事实参考，但不含晃动翻页这类功能实现，硬件问题可对照其板级目录排查。
Vendor repo local path: `~/codespace/Metalio-E-INK4-Plus` (`~` denotes the current user's home directory). From now on, "原厂/vendor" refers to this checkout. Its firmware carries the hardware groundwork (SC7A20H driver, AOI1 motion interrupt and alike) and is the reference for hardware facts, but it does not implement features like shake page turning; debug board-level questions against its board directory.

## Git 提交 / Git commits

今后的提交必须遵循 [中文提交规范](docs/COMMIT_CONVENTION.md)：标题使用 `分类（范围）：中文摘要`，正文按分类顺序从 `1.` 连续编号，每项只写一件事，最后如实说明验证结果。标题、分类和正文均使用中文，技术标识保留原文；agent 提交末尾注明 `代理：<名称>`。历史提交不改写。
Follow the [Chinese commit convention](docs/COMMIT_CONVENTION.md) for all future commits: Chinese category and subject, consecutively numbered changes in category order, and truthful validation results. Keep technical identifiers unchanged, add a Chinese agent label for agent commits, and preserve existing history.

## 构建 / Build

需要 ESP-IDF v6.1；ESP32-S31 仍为 preview，构建命令必须带 `--preview`。
Requires ESP-IDF v6.1; ESP32-S31 is a preview target, so build commands require `--preview`.

```
idf.py --preview set-target esp32s31
idf.py --preview build
```

- `sdkconfig.defaults`：S31 产品配置，flash 80 MHz、Octal PSRAM 200 MHz。/ S31 product configuration: 80 MHz flash and 200 MHz octal PSRAM.
- `sdkconfig.ci`：在产品配置之后加载的编译警告叠加项。CI 使用 `-DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci"`，同样编译 S31。/ Compile-warning overlay loaded after product defaults; CI builds S31 with the same defaults list.

CI 只证明能编译；真机时序与行为仍须设备验证。/ CI proves compilation; device timing and behavior still need hardware validation.

### 版本号与本地测试 / Versions and local tests

用户 2026-11-05 要求放宽脚本时限，取代此前两分钟总预算；本仓库必须优先使用项目级 [kimiko-e4p-flash skill](.agents/skills/kimiko-e4p-flash/SKILL.md) 和 `python3 tools/flash_fast.py flash --yes`。脚本默认及上限改为 180 秒，可用 `--timeout 1..180` 缩短本次时限。刷机阶段不重复全量测试、索引或全片备份，不使用固定活动槽地址，不自动退回整机擦写；新功能在开发构建阶段先用 `prepare --build-dir` 准备当前产品构建。冷构建、设备异常或兼容性检查失败时及时停止说明，不刷旧包或虚报成功；超过时限的准备/恢复需另行说明并获授权。skill 仅放在本仓库 `.agents/skills/`，不安装到全局。设计/测试工具不代表刷机授权。
The user's 2026-11-05 request to relax the script deadline supersedes the two-minute total budget. Use the repository-scoped skill and helper, now defaulting to a 180-second maximum; `--timeout 1..180` can shorten a run. Prepare known product builds during development and omit redundant tests/indexing/full-flash backups during flash-only work. Never hardcode the active slot, fall back to whole-device writes, substitute stale firmware or claim failure as success. Longer preparation/recovery needs separate authorization; tool creation/testing does not authorize flashing.

快速刷机使用官方 S31 RAM stub，不回退到已复现连续读取故障的 ROM 读取接口。180 秒中预留最后 10 秒清理：失败且未写入或已完整校验时，若尚未尝试启动，应复位并确认原应用/首页恢复；仅关闭串口不算恢复。部分写入未校验时不启动未知应用，不自动重刷；已尝试启动仍失败时不循环复位，按实际状态报告。
Use the official S31 RAM stub rather than the failing ROM read path. Reserve the last ten seconds for cleanup: safely reset and confirm an unbooted intact application after failure. Port closure alone is not recovery. Do not boot an incomplete write, retry flashing or repeatedly reset an unconfirmed boot.

用户 2026-10-06 确认：本地测试沿用当前正式发布的固件版本号，用测试日期和说明区分；正式上线时才递增版本号，并同步设备、刷机页、升级包与日志。不要为每次本地修复另增 rc 编号。
Per the user's 2026-10-06 decision, local tests retain the current published firmware version and are distinguished by test date and notes. Increment only for publication, keeping the device, flasher, upgrade image and changelog in sync; do not add an rc for each local fix.

本地包放在被忽略的构建目录，禁止用同版本的测试二进制覆盖 `flash/` 中已经发布的固定版本下载文件。联网 OTA 需要不同版本号的专项测试时，先获得用户对该测试版本的明确授权。
Keep local bundles in ignored build directories; never replace immutable published downloads in `flash/` with a same-version test image. Use a different version for an OTA-specific test only when explicitly authorized by the user.

### OTA 版本更新日志 / OTA release notes

每次用户要求创建 OTA 版本标签时，基于上一个对应发布版本到本次提交的实际差异，先整理准确的中文更新日志，写入 `docs/ota-release-notes/<版本号>.txt` 并与代码一起提交，然后再创建版本标签。日志以 UTF-8 编码，去除首尾空白后不得为空或超过 383 字节；不要虚构功能、修复或验证结果。标签构建自动读取对应文件，随固件提交到平台草稿；发布仍须管理员审核。已存在的版本或标签不得覆盖或重新指向。

For each OTA version tag, summarize actual changes since the appropriate previous release in `docs/ota-release-notes/<version>.txt`, commit it before tagging, and keep the nonempty UTF-8 text within 383 bytes. Never invent changes or verification. The workflow submits it with the firmware draft; an administrator still reviews and publishes. Do not overwrite existing versions or move tags.

### 正式版发布 / Official releases

用户 2026-11-03 确认：今后提到「发布版本」，均指发布到本仓库当前 `origin` 远端，替代此前固定的旧发布仓库；发布前核对 `git remote get-url origin`，当前为 `siliscar/kimiko-e4p`。推送代码，创建并推送对应版本的 Git tag，触发 OTA 网站的固件草稿构建与提交，并在同一远端创建 GitHub Release。附上经验证的 TF/OTA 应用 Bin、完整 USB 刷机包、SHA-256 校验值和简要更新日志；所有附件与 OTA 提交必须来自同一次正式构建。OTA 草稿仍须管理员审核发布，不得把草稿提交成功视为已经上线。
Per the user's 2026-11-03 decision, "publish a version" means publishing to this repository's current `origin`, replacing the previously fixed legacy release repository. Check `git remote get-url origin` before publishing; it currently points to `siliscar/kimiko-e4p`. Push the code and a matching Git tag to trigger the OTA website's draft build/submission, and create a GitHub Release in the same remote repository. Attach the verified TF/OTA application Bin, complete USB flash bundle, SHA-256 checksums and brief release notes; all attachments and the OTA submission must come from the same official build. OTA drafts still require administrator review and publication; successful submission does not mean they are live.

用户 2026-11-04 确认 GitHub Release 的展示与命名规范如下；`<版本号>` 不含 `v`，Git tag 仍使用 `v<版本号>`（例如 `v1.0.3`）。
Per the user's 2026-11-04 decision, use the following GitHub Release presentation and naming rules. `<version>` excludes `v`; Git tags remain `v<version>` (for example, `v1.0.3`).

| 项目 / Item | 格式 / Format | 1.0.3 示例 / Example |
| --- | --- | --- |
| Release 标题 / Release title | `kimiko E4p <版本号>` / `kimiko E4p <version>` | `kimiko E4p 1.0.3` |
| TF/OTA 应用镜像 / TF/OTA application | `kimiko-e4p-update-<版本号>.bin` / `kimiko-e4p-update-<version>.bin` | `kimiko-e4p-update-1.0.3.bin` |
| USB 单文件整机镜像 / Single-file USB image | `kimiko-e4p-<版本号>-full.bin` / `kimiko-e4p-<version>-full.bin` | `kimiko-e4p-1.0.3-full.bin` |
| 完整 USB 刷机包 / Complete USB bundle | `kimiko-e4p-<版本号>-usb.zip` / `kimiko-e4p-<version>-usb.zip` | `kimiko-e4p-1.0.3-usb.zip` |

Release 说明按更新日志、下载、验证组织，下载文件名与实际附件、包外 `README.txt`、`SHA256SUMS` 保持一致，保留 `release-provenance.json` 构建来源。保留当前目标硬件与刷写方式说明，不再添加「不适用于旧ESP32-S3设备」一句。以上仅调整 GitHub Release 的标题、下载文件名及说明，不改变固件界面品牌、目标硬件、构建部件名、备份文件名、内部协议或官网/OTA 地址；未经用户明确要求，不更名或替换已经发布的附件。
Organize Release descriptions as updates, downloads and verification. Keep download names consistent across assets, the outer `README.txt` and `SHA256SUMS`, and retain build provenance in `release-provenance.json`. Describe the current target hardware and flashing methods without adding the old ESP32-S3 exclusion sentence. These rules affect only GitHub Release titles, download filenames and descriptions, not firmware UI branding, hardware targets, build component names, backup filenames, internal protocols or website/OTA URLs. Do not rename or replace published assets without an explicit user request.

GitHub Release 与官网刷机、TF 下载和联网 OTA 同步发布。保留官网现有刷机清单、固件及版本下载地址；OTA 清单继续使用固件允许的官方 HTTPS 域名。核对三种交付的版本、大小、校验值和基础包兼容性，避免影响正在刷机或升级的用户。本地测试仍只生成本地包，不创建正式 Release。
Publish GitHub Releases alongside the web flasher, TF downloads and online OTA. Preserve existing website manifests, firmware and versioned download URLs; keep OTA URLs on the official HTTPS origin accepted by the firmware. Verify matching versions, sizes, checksums and base-layout compatibility across the deliveries so ongoing flashes and updates remain valid. Local tests remain local bundles without official Releases.

## 目录职责 / Layout

| 路径 / Path | 职责 / Role |
| --- | --- |
| `main/app_main.c` | 开机装配：板级 init、读设置、定 VCOM、开机图、字体，然后交给 `app_loop`。/ Boot wiring, then `app_loop`. |
| `main/app/app.h` | `app_desc_t` / `app_ctx_t` / `app_redraw_t` 契约。/ Page contract. |
| `main/app/app_loop.c` | 与页面无关的事件循环：触摸去抖、三键与菜单把手、切页、刷屏、锁屏、SD 字体延迟加载。/ Shared event loop. |
| `main/app/app_registry.c` | 菜单顺序。加页只改这张表。/ Menu table. |
| `main/app/app_content_open.h` `app_transfer_mode.h` `app_device_settings_mode.h` | 跨页面请求契约，由目标页面消费。/ Cross-page request contracts consumed by target pages. |
| `main/app/app_config.h` | 均衡 / ALL_DU 刷新档位。/ Refresh profile. |
| `main/ui/` | `ui_kit` 排版常量与绘制原语；`ui_menu` 两层菜单。页面互不引用。/ Shared drawing; pages do not include each other. |
| `main/font/` | stb_truetype 字形缓存。`fallback.h` 是生成位图，勿手改。/ Glyph cache. Do not edit `fallback.h` by hand. |
| `main/image/` | 公共图像探测、解码与缩放，不依赖书籍、页面或字库。/ Shared image probing, decoding and scaling without book, page or font dependencies. |
| `main/services/` | 跨页 USB 存储、字体上下文、文件任务交还与触摸震动。/ Cross-page USB storage, font contexts, file-worker handoff and touch haptics. |
| `main/factory/` | 设备功能自检、出厂 VCOM 标定与 EPD 测试图案。/ Device self-test, factory VCOM and EPD test patterns. |
| `main/boot/` | 启动保护与恢复检查点。/ Boot protection and resume checkpoints. |
| `main/display/` | 显示出口、几何、波形与灰阶归一。/ Display entry points, geometry, waveforms and gray normalization. |
| `main/lock/` | 自动锁屏判定、密码与锁屏图缓存。/ Idle-lock decisions, PIN and lock-frame caches. |
| `main/power/` | 睡眠、唤醒、关机与缓存恢复。/ Sleep, wake, power-off and cache restore. |
| `main/settings/` | NVS 设置与备份恢复。/ NVS settings and backup/restore. |
| `main/ota/` | TF/联网升级、清单与下载校验。/ Card/online upgrades, manifests and download verification. |
| `main/time/` | 时间服务与校时策略。/ Time services and sync policy. |
| `main/diagnostics/` | 阅读计时与资源诊断。/ Reader timing and resource diagnostics. |
| `main/platform/` | S31 输入适配。/ S31 input adaptation. |
| `main/assets/` | 内置资源与生成数据。/ Bundled resources and generated assets. |
| `components/board_support/` | 板级 BSP：I2C、EPD 板定义与扫描时序、TF 卡、蜂鸣器、flash HPM。/ Board BSP. |
| `components/book_search/` | 离线拼音/首字母/英文书名匹配，设备和网页共用；字表由工具生成。/ Shared offline pinyin, initials and English matching with a generated table. |
| `components/device_power/` | 电量、充电、RTC 与电源键接口；S31 选 Metalio 后端，旧 CW32 线格式在 `pmu_protocol.h`。/ Battery, charging, RTC and power keys; S31 selects Metalio, with the legacy CW32 wire format in `pmu_protocol.h`. |
| `components/wifi_transfer/` | 热点与已有 WiFi 传书、配网和校时会话。/ AP/STA book transfer, provisioning and time-sync sessions. |
| `components/epdiy/` | 上游裁剪 fork（LCD 路径）。改动清单在其 LICENSE。不要为注释去改它。/ Trimmed upstream fork. Leave it alone for comment work. |
| `components/cst836u/` `sc7a20h/` `fca9555/` | 当前触摸、加速度计及扩展口驱动；旧名称包含 FT6336U/TCA9555 适配。/ Current touch, accelerometer and expander drivers, including FT6336U/TCA9555 adaptations under legacy names. |
| `components/e0470_epaper_waveform/` | 面板波形与裁剪。`waveforms/*.h` 是数据表。/ Panel waveforms. |
| `docs/CHANGELOG.md` | 按版本简述对用户可见的功能变化与修复。/ Brief user-visible changes and fixes by version. |

main 根目录仅保留启动入口与构建配置，新增功能文件按上表职责归类。/ Keep only boot assembly and build configuration at the main root; group new feature files by the ownership table above.

功能操作见三份 README；页面契约以本文及 `main/app/app.h` 为准。/ See the READMEs for feature usage; this file and `main/app/app.h` define the page contract.

### 文档交付边界 / Documentation delivery boundary

对外交付功能实现、可复跑测试、功能说明及必要的维护契约。功能变化只在 `docs/CHANGELOG.md` 按版本轻量记录；代码演进由 Git 管理，不另建逐任务变更账本。
Deliver implementation, reproducible tests, feature documentation and necessary maintenance contracts. Summarize feature changes only in `docs/CHANGELOG.md`; Git owns code history, not per-task ledgers.

计划、检查点、过程审查、验收结果和调试日志在 `docs/local/` 或被忽略的构建目录内部闭环，不加入提交。长期有效的行为与限制整理进对应功能文档，不要求读者查内部计划。只有需要交接未完功能或责任时才按 `docs/HANDOFF.md` 提供最小必要摘要；本机账本仍不入库。
Keep plans, checkpoints, review records, acceptance results and debug logs in ignored `docs/local/` or build directories. Put lasting behavior and limits in feature documentation without dependencies on internal plans. Use `docs/HANDOFF.md` for the minimal necessary handoff of unfinished work or responsibility; the local ledger remains untracked.

用户2026-11-02要求持续记录夜间阅读显示/残影试验：每轮先读 `docs/local/reader-night-mode-experiments.md`，按连续编号追加基准、变量、授权、检查/刷入过程、实际样本、用户反馈、结果与设备最终状态；每次尝试或收到反馈后及时更新。区分候选、失效试验、软件验证与实屏结论，未采集/未确认项如实留空；该本地记录不入库，持续记录不代表持续刷机授权。接手入口保留在 `docs/HANDOFF.local.md`。
Per the user's 2026-11-02 request, maintain the local night-reader display/ghosting log at `docs/local/reader-night-mode-experiments.md`: read it before each trial and append sequential IDs, baseline, variable, authorization, checks/flashing procedure, actual samples, user feedback, outcome and final device state. Update after every attempt or subsequent feedback. Distinguish proposals, invalid trials, software evidence and optical results; leave unmeasured items explicit. Keep this log uncommitted and linked from `docs/HANDOFF.local.md`; ongoing logging does not authorize ongoing flashing.

用户同日最终决定撤回夜间模式，不作为正式功能上线；N00–N21归档后停止试验。后续必须重新取得明确授权，不得沿用旧许可继续刷机或恢复实验功能。
The user's final decision that day withdraws night mode from release and stops experiments after archiving N00–N21. Any future trial requires renewed explicit authorization; do not reuse prior permission to flash or restore the experimental feature.

### 产品页 / Product pages

页面入口以 `main/CMakeLists.txt` 和 `app_registry.c` 为准；旧板演示页已移除。每页导出 `app_desc_t`，页面之间不直接依赖。
Use `main/CMakeLists.txt` and `app_registry.c` as page entry points; legacy board demos have been removed. Each page exports an `app_desc_t` and does not depend directly on other pages.

| 文件 / File | 职责 / Role |
| --- | --- |
| `app_dashboard.c` | 首页题头、头像及上下等高组件区；最近阅读/天气唯一，天气直角铺满。/ Home header, avatar and equal widget regions; reading/weather are unique, with edge-to-edge rectangular weather. |
| `app_components.c` | 组件库与天气城市选择，契约见 `docs/development/guides/WEATHER.md`。/ Component library and weather city picker; see the weather contract. |
| `app_book.c` | 书架与 TXT / EPUB 阅读。/ Shelf and TXT / EPUB reading. |
| `app_files.c` | 文件浏览与传输入口。/ Files and transfer entry points. |
| `app_device_settings.c` | 设备设置、蓝牙翻页与维护。/ Settings, Bluetooth page-turners and maintenance. |
| `app_image.c` | 内置灰阶图与卡上 JPG / PNG。/ Built-in gray chart and SD JPG / PNG. |
| `app_transfer.c` | 热点 / 已有 WiFi 传书与配网，离页停止。/ AP / STA transfer and provisioning; stops on exit. |
| `app_boot_recovery.c` | 启动恢复；不在根导航中。/ Boot recovery, outside root navigation. |

出厂 VCOM 与自检流程仍由 `main/factory/` 和启动入口调用。新增页面需同时加入构建清单和注册表；锁屏前保存状态使用 `on_before_lock`。
Boot and `main/factory/` retain VCOM and self-test flows. Add new pages to both the build list and registry; use `on_before_lock` to save state before locking.

## `app_desc_t` 契约 / Contract

定义在 [`main/app/app.h`](main/app/app.h)。主循环按返回值刷屏，见 [`main/app/app_loop.c`](main/app/app_loop.c) 的 `app_present()`。

Defined in [`main/app/app.h`](main/app/app.h). The loop presents via `app_present()` in [`main/app/app_loop.c`](main/app/app_loop.c).

| 返回值 / Return | 主循环做什么 / Loop does |
| --- | --- |
| `APP_REDRAW_NONE` | 不刷。/ No update. |
| `APP_REDRAW_AREA` | 只推 `area_hint()`。回调必须先画好 fb。模式是 `APP_DYNAMIC_REFRESH_MODE`（DU）。/ Push `area_hint()` only. Caller paints fb first. |
| `APP_REDRAW_PAGE` | 调 `render()`，整页 `APP_PAGE_REFRESH_MODE`（均衡档是 GL16）。/ `render()` then page mode. |
| `APP_REDRAW_FULL` | 调 `render()`；`APP_PAGE_FORCE_FULL` 时整屏 GC16，否则仍走页模式。/ `render()`; GC16 when force-full is on. |
| `APP_REDRAW_DONE` | 页面自己已经刷完。/ Page already presented. |

回调纯度：

- `render()`：纯绘制。禁止 I2C 写、蜂鸣、睡眠、改设置。KEY2 整屏强刷会复用它。/ Paint only. No I2C writes, buzzer, sleep, or settings. KEY2 reuses it.
- `on_enter()`：上电、唤醒传感器、拉一次数据。/ Power-up and first sample.
- `on_before_lock()` 可选：锁屏绘制前保存阅读进度与统计。/ Optional pre-lock flush of reading progress and statistics.
- `on_media_lost()` 可选：主循环检测已挂载卡失效时调用，先停止文件/后台消费者，不画屏、不重挂载；返回后主循环回退字体并重绘。/ Optional mounted-media-loss callback: stop file/background consumers without drawing or remounting; the loop then falls back fonts and redraws.
- `on_exit()`：掉电、停传感器。/ Power-down.
- `present()`：自定义推屏。返回 true 表示已经刷过，主循环不再推。/ Custom present; true means done.
- `on_touch()` / `on_gesture()` / `on_key()` / `on_tick()`：可有副作用，用返回值要刷屏。/ Side effects OK; return the redraw.
- `on_key_long()` 可选：仅接管三键的页面在同键单指保持500ms后触发一次；`defer_middle_short` 页面中键短按在抬起时执行，长按不触发短按；滑出、多点、读错、睡眠、字体重载或切页取消。/ Optional owned-key hold callback fires once at 500ms; with `defer_middle_short`, a middle short press fires on release and a hold skips it. Leaving, multitouch, read errors, sleep, font reload and page switches cancel.
- `on_power_short()` 可选：页面可消费电源键短按；返回 `APP_REDRAW_NONE` 时主循环继续锁屏。阅读正文的“电源键翻页”开关开启后短按翻下一页，长按由主循环锁屏；其他页面保留原电源键逻辑。/ Optional short-power callback may consume the press; `APP_REDRAW_NONE` keeps the lock path. With reader power-turn enabled, short presses advance in the reader body and long presses lock; other pages keep their usual power behavior.
- `on_media_ready()` 可选：首次无卡启动后插卡或显式重挂完成时通知当前页面更新目录；不要用它绕过拔卡后的句柄清理。/ Optional callback refreshes page listings after a first insertion or explicit remount; it must not bypass handle cleanup after removal.
- `on_gesture()` 可选；提供后不再接收 `on_touch()`。主循环负责识别与全局中断取消，页面解释动作。/ Optional gesture callback replaces `on_touch`; the loop recognizes and cancels, the page interprets.
- `normalize_hold_gray()` 可选：归一属性按当前画面判断，只有显示阅读背景正文时返回 true。不要用全局背景设置代替这个判断。快捷栏收起整屏归一与阅读呈现共用此属性。/ Optional: derive normalization attributes from the visible frame, returning true only for reading text over a background. Never substitute the global background setting. Whole-screen quick-layer close normalization and reader presentation share this property.
- `on_key()` 默认只收 `UI_KEY_1`；`owns_keys` 页面在菜单关闭时接收三键。主页面左右键循环切换四个主页，中键长按全刷；书架中键短按切换分页控制并沿左右箭头轮廓描边，可通过导航随时打断。系统子页左返回、中首页、右返回；阅读正文三键短按可自定义，两套推荐默认上一页/工具栏/下一页；底部上滑固定呼出工具栏，三键短按和中键长按不强制工具栏映射；中键长按可自定义，默认返回首页；中键双击可自定义，默认不操作；全屏由工具栏「全屏」按钮或「切换全屏」按键动作切换，正文中央双击不再切全屏；新开书沿用记住的全屏偏好（默认全屏），翻页收起工具栏与滑块但不改变全屏状态。/ Normally KEY1 only; `owns_keys` pages receive all three outside the menu. Root pages use left/right to cycle and a middle hold for full refresh; shelf middle short press toggles paging controls. Product system pages use Back/Home/Back; reader short actions are configurable, defaulting to previous/tools/next; bottom-edge swipes always open the toolbar without a required short/hold mapping; middle holds and middle double presses are configurable (the double press defaults to no action); full screen is toggled by the toolbar button or a full-screen key action, and center body double taps no longer toggle it; new books follow the remembered full-screen preference (default on) and turns dismiss tools and sliders without changing full screen.

标志：

- `holds_pmu`：长时间独占 PMU（自检）。主循环的锁屏按键轮询让路。/ Page owns the PMU; lock-key poll yields.
- `owns_keys`：接管三键；不影响菜单打开时的全局键和菜单把手。/ Own all three keys outside the menu; global menu keys and the handle remain.
- `defer_middle_short`：中键短按在抬起时触发，以区分长按。/ Defer middle short action until release to distinguish a hold.
- `menu_handle_enabled`：按页面状态决定右下角菜单把手是否响应。/ Control bottom-right menu-handle hit testing by page state.
- `enter_full`：进页走 `APP_REDRAW_FULL`，避免差分刷留边。/ Enter with a full refresh.

`app_ctx_t.request_app` 非空时，主循环在本轮末尾切页；`request_menu` 请求打开根菜单。同时设置时切页优先。/ At tick end, `request_app` switches pages and `request_menu` opens the root menu; a page request takes priority.

## 术语表 / Glossary

| 术语 / Term | 中文 | English |
| --- | --- | --- |
| DU | 快速差分刷新，只驱动变化像素，灰阶少、残影多。 | Fast differential update. Drives changed pixels only. |
| GC16 | 16 级灰度全像素刷新。区域内白像素先压黑再擦白，刷新处一条黑带。 | 16-gray full-pixel update. Whites go black then white; a black flash. |
| GL16 | 16 级灰度，白底保持，不先压黑。 | 16-gray update that keeps an already-white background. |
| 跟随 DU / FOLLOW DU | 开机生成的 8 帧短波形，一次刷新连 diff 带扫描，用来跟手数字或读数。 | Boot-built 8-frame short waveform: one refresh does diff + scan. Used for live digits. |
| 连续 DU / continuous DU | 每轮只扫一个相位，软件记每像素剩余相位。帧率与黑度解耦。 | One DU phase per scan; software tracks leftover phases per pixel. |
| 跟手 | 手指或传感器在动时用 DU/连续 DU 追画面。 | Live tracking with DU / continuous DU while the finger or sensor moves. |
| 定稿 | 动作停下后用 GC16/GL16 整页（或指定区）清残影。 | Settle after motion with GC16/GL16 to clear ghosting. |
| 把手 | 底栏菜单按钮；演示页的 KEY3 也可触发。打开/关闭全屏一级菜单。 | Bottom-right menu handle; KEY3 can also toggle it on demo pages. |
| 冻结 / Frozen | 文件头里的产品决策。agent 不得改行为去“优化”它。 | Product decisions in the file banner. Do not “improve” them. |
| 均衡配置 | `APP_REFRESH_BALANCED`：整页 GL16，周期 GC16 压灰底，动态区 DU。 | Default refresh profile: GL16 pages, periodic GC16, DU for motion. |
| ALL_DU | 实验档：整机都走 DU，只看速度。 | Experimental profile: everything is DU. |
| 锁屏 | 电源键短按后的锁页。浅睡按键或拿起回原页。 | Lock face after a short power-key press. |
| 软睡 | `SOFT_SLEEP`：PMU 拉低 EN，再短按开机，不是硬复位。 | Soft sleep: PMU drops EN; a short press powers on without a hard reset. |
| VCOM | 面板公共电压（绝对值 mV）。出厂写入 PMU，主机只读。 | Panel common voltage (absolute mV). Factory-written in the PMU; host is read-only. |

注释里不要再展开这些词。/ Do not re-explain these terms in comments.

## 硬约束 / Hard rules

- 用户 2026-11-04 明确将固件与项目品牌统一改为小写 `kimiko`，取代此前品牌决策；设备默认名、热点前缀、固件界面与传书网页均使用 `kimiko`。仅迁移历史默认设备名，保留用户自定义名称；热点口令为满足 WPA2 长度要求的 `kimiko1234`。网页设备仍称「墨水屏」，页面底色保持明亮纯白；GitHub Release 的标题和下载文件名继续按上述发布规范执行。源码与新版本发布使用本仓库当前 `origin`（现为 `siliscar/kimiko-e4p`）；保留已发布下载/OTA 地址、备份文件名、内部协议标识以及历史许可标识和真实版权声明。天气和 OTA 的平台接口域名已移除，需自建平台后在 menuconfig 配置 `CONFIG_PICO_COMPONENT_API_ORIGIN` 与 `CONFIG_PICO_OTA_FEED_URL` 才会启用。
  Per the user's 2026-11-04 rebranding decision, use lowercase `kimiko` for the firmware and project, including default device names, hotspot prefixes, firmware UI and transfer pages. Migrate only historical default device names, preserving custom names; use `kimiko1234` for the WPA2 hotspot password. Keep the bright-white website and device label “墨水屏”, existing Release naming rules, current `origin`, published download/OTA URLs, backup names, protocol identifiers, historical license identifiers and accurate copyright notices. Weather and OTA platform origins are removed; configure `CONFIG_PICO_COMPONENT_API_ORIGIN` and `CONFIG_PICO_OTA_FEED_URL` in menuconfig for a self-hosted platform to enable them.

`冻结 / Frozen:` 段落是产品决策，不是建议。改行为前必须先改这段，并说明为什么决策变了。

A `冻结 / Frozen:` block is a decision, not a hint. Change the behavior only after rewriting that block and saying why the decision changed.

- 面板 VCOM 存在 PMU 里。主机开机读一次，不写 ESP NVS，产品页不提供修改入口。标定页（`vcom_setup.c`）是未标定时的出厂拦住路径。/ VCOM lives in the PMU. Host reads once. No NVS copy. No product UI to edit it. `vcom_setup.c` is the factory gate when unset.
- 不要写 SY7636A 的 VCOM / VLDO / 放电 / 延时 / VCOMCTL。电源页只读。/ Do not write SY7636A VCOM / VLDO / discharge / delay / VCOMCTL. Power page is read-only.
- S31 使用 flash 80 MHz、Octal PSRAM 200 MHz；旧 S3 的 Zbit HPM 覆盖不参与 S31 构建。/ S31 uses 80 MHz flash and 200 MHz octal PSRAM; the legacy S3 Zbit HPM override is outside the S31 build.
- 不要改 `components/epdiy/**` 来“整理注释”。/ Do not touch `components/epdiy/**` for comment cleanup.
- 不要手改 `main/font/fallback.h`、`main/font/stb_truetype.h`、`components/e0470_epaper_waveform/waveforms/*.h`。/ Do not hand-edit generated or vendored tables.
- 不改 UI 字面量或日志字符串，除非任务明确要求。/ Do not change UI strings or log text unless asked.

## 注释规范 / Comment style

### 授权声明 / License notices

用户 2026-10-15 要求新增原创内容仅限个人学习、不得未经书面授权商用。适用边界以根目录 [LICENSE](LICENSE) 和 [CONTRIBUTING.md](CONTRIBUTING.md) 为准；这是今后明确标注的新原创内容的许可，不撤销 rc93 及以前的授权，不覆盖第三方或其他贡献者的权利。新原创文件只有在权利人明确同意时使用 `LicenseRef-Kiikoread-Personal-Study-1.0`，保留真实版权人。不要将下面的 Apache 文件头示例机械用于新原创文件，也不要批量替换既有文件头。
Per the user's 2026-10-15 decision, explicitly marked new original material is for personal study, with commercial use requiring written permission. [LICENSE](LICENSE) and [CONTRIBUTING.md](CONTRIBUTING.md) define the scope; rc93 and earlier grants and third-party or other contributors' rights remain intact. Use `LicenseRef-Kiikoread-Personal-Study-1.0` on new original files only with the rights holder's explicit consent and accurate attribution. Do not mechanically apply the Apache example below to new original files or replace existing headers in bulk.

全仓中英双语。只改注释，不改逻辑。

Every comment is bilingual. Comments only; no logic changes.

文件头：

```
/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：这个文件负责什么、边界在哪。
 *
 * English: what this file owns and where it stops.
 *
 * 冻结：不得做的事。
 * Frozen: things this file must not do.
 */
```

- 头文件公开 API 与 struct 成员用 `///`。/ Public API and struct fields: `///`.
- `.c` 内部用 `//`。不要 `/** */`。/ `.c` internals: `//`. No `/** */`.
- 单行：`// 中文。/ English.`
- 超过一行：中文一行，英文紧跟一行。/ Multi-line: Chinese line, then English line.
- `enum` 成员短尾注：`///< 中文 / English`
- 大 `.c` 用 `/* ---- 区块名 / Section ---- */` 分区。/ Large `.c` files use section banners.
- 术语见表，不在注释里重复解释。/ Use the glossary; do not redefine terms.

<!-- gitnexus:start -->
# GitNexus — Code Intelligence

This project is indexed by GitNexus as **kimiko-e4p** (16298 symbols, 32232 relationships, 517 execution flows).

> Index stale? Run `node .gitnexus/run.cjs analyze --index-only` from the project root — it auto-selects an available runner. No `.gitnexus/run.cjs` yet? Bootstrap with `npx`, `bunx`, or `pnpm dlx` — e.g. `bunx gitnexus@latest analyze` (npm 11 npx crash; #1939).

## Always Do

- **MUST run impact before editing.** Use `impact({target: "symbolName", direction: "upstream"})` or `node .gitnexus/run.cjs impact "symbolName" --direction upstream --repo .`; report callers, processes, and risk. Never substitute grep for graph analysis.
- **MUST analyze graph changes before committing.** Use `detect_changes({scope: "all"})` (MCP) or `node .gitnexus/run.cjs detect-changes --scope all --repo .` (CLI fallback). `partial: true` or `truncated: true` is not a clean check — a zero means unseen, not unaffected; re-run it. For regression review: `detect_changes({scope: "compare", base_ref: "main"})` or `node .gitnexus/run.cjs detect-changes --scope compare --base-ref "main" --repo .`.
- MUST warn on HIGH/CRITICAL `risk` pre-edit; never use `riskSharedAxes` to waive a HIGH/CRITICAL `risk` warning. Compare File/symbol: MCP File omits axes; Graph-RAG expands File.
- **MUST treat `risk: UNKNOWN` as unresolved, not as low.** An empty caller set is not evidence the symbol is unused — it can also mean the callers are not resolvable by the index (plain-object property access, dynamic dispatch, cross-language calls). `impact` pairs `UNKNOWN` with a `riskNote` saying so. Confirm with a text search before treating the symbol as safe to change or delete; do not proceed on the strength of a zero.
- **MUST use `query({search_query: "concept"})` for concepts/flows, `context({name: "symbolName"})` for a named symbol, or `impact` for blast radius, on read-only callers, dependencies, imports, or execution flow.** Graph first; text search only for empty/`UNKNOWN`/literals.
- For security review, `explain({target: "fileOrSymbol"})` lists taint findings (source→sink flows; needs `analyze --pdg`).

## Never Do

- NEVER edit a function, class, or method before MCP/CLI impact analysis.
- NEVER ignore HIGH or CRITICAL risk warnings from impact analysis, and never read `UNKNOWN` as an all-clear — it means the walk could not answer, which is the one verdict that requires confirming by other means.
- NEVER rename symbols with find-and-replace — use `rename` which understands the call graph.
- NEVER commit before MCP/CLI graph change analysis.

## Resources

| Resource | Use for |
| --- | --- |
| `gitnexus://repo/kimiko-e4p/context` | Codebase overview, check index freshness |
| `gitnexus://repo/kimiko-e4p/clusters` | All functional areas |
| `gitnexus://repo/kimiko-e4p/processes` | All execution flows |
| `gitnexus://repo/kimiko-e4p/process/{name}` | Step-by-step execution trace |

## CLI

| Task | Read this skill file |
| --- | --- |
| Understand architecture / "How does X work?" | `.claude/skills/gitnexus-exploring/SKILL.md` |
| Blast radius / "What breaks if I change X?" | `.claude/skills/gitnexus-impact-analysis/SKILL.md` |
| Trace bugs / "Why is X failing?" | `.claude/skills/gitnexus-debugging/SKILL.md` |
| Rename / extract / split / refactor | `.claude/skills/gitnexus-refactoring/SKILL.md` |
| Tools, resources, schema reference | `.claude/skills/gitnexus-guide/SKILL.md` |
| Index, status, clean, wiki CLI commands | `.claude/skills/gitnexus-cli/SKILL.md` |

<!-- gitnexus:end -->

<!-- tidy:start -->
## 项目开发治理

先读取 `.tidy.json` 与 `docs/development/INDEX.md`。提交信息遵守 `docs/COMMIT_CONVENTION.md`。
使用 `$tidy` 执行提交前文档/架构/功能检查和提交后 GitNexus 刷新。
tidy 更新后使用 `$tidy-rebuild` 评估并对齐现有治理；制定或维护自动化发版流程使用 `$tidy-release`。
本次记录放 `docs/local/tidy/changes/`；过程材料遵守已确认的可见性，不取消既有忽略边界。
普通计划、设计草稿、图片、过程决策和验收摘要放 `docs/local/tidy/` 对应分类；工具固定路径优先，稳定共享文档另按项目政策维护。
GitNexus 计划保留 `docs/plans/*gitnexus-plan*.md` 原路径与正文，tidy 只建索引，不搬迁、改名、复制或用符号链接替代。
本地过程策略与 `gitnexus-work` 的计划入库要求冲突时，先确认可见性与当前提交授权；不强制入库或擅自改变忽略规则。
提交前按规范归类全部实际变化并维护统一入口；仅暂存本任务内容。
每次提交前检查并清理本任务的临时日志、断点、测试入口及调试桩；保留正式日志/回归测试，清理后验证并运行 check --staged。
提交成功后刷新 `gitnexus analyze --index-only`，核对新鲜度并更新本地回执；失败如实报告。
不因治理流程获得 push、历史改写、发布或硬件操作授权。现有规则冲突先询问用户。
<!-- tidy:end -->
