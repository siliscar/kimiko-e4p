# 开发上手 / Developer onboarding

本仓库维护 mogo 在 Metalio E-INK4 Plus（ESP32-S31）上的阅读固件。旧 S3 板和演示固件的历史设计不能作为当前构建或硬件参数。
This repository maintains the mogo reader firmware for Metalio E-INK4 Plus (ESP32-S31). Historical S3/demo designs are not current build or hardware specifications.

## 阅读顺序 / Reading order

1. [README](../README.md)：产品目标与构建入口。/ Product target and build entry.
2. [AGENTS.md](../AGENTS.md)：硬约束、页面契约、版本与提交规则。/ Hard rules, page contracts, versions and commits.
3. [架构说明](ARCHITECTURE.md)：目录职责和依赖边界。/ Directory ownership and dependency boundaries.
4. [交接协议](HANDOFF.md)：接手未完任务时读取本机 HANDOFF.local.md，再对照实际工作区。/ For unfinished work, read local HANDOFF.local.md and verify it against the checkout.

行为以当前代码和文件头 Frozen 决策为准。README 是产品、安装与构建入口；早期移植评估保留在 docs/development/history/porting，不覆盖当前 CMake、sdkconfig.defaults 或页面契约。
Current code and file-header Frozen decisions define behavior. README covers the product, installation and builds; early port assessments remain in docs/development/history/porting and do not override current CMake, sdkconfig.defaults or page contracts.

## 构建环境 / Build environment

使用 ESP-IDF v6.1，产品仅接受 esp32s31；preview 目标必须传 --preview。先激活对应 IDF 环境，确认 idf.py 的版本。
Use ESP-IDF v6.1 and esp32s31 only; the preview target requires --preview. Activate the matching IDF environment and confirm idf.py's version.

```sh
idf.py --version
idf.py --preview set-target esp32s31
idf.py --preview build
```

- sdkconfig.defaults 是产品配置：flash 80 MHz、Octal PSRAM 200 MHz。sdkconfig.ci 仅叠加编译警告，不能单独使用。
  sdkconfig.defaults defines the product: 80 MHz flash and 200 MHz octal PSRAM. sdkconfig.ci only adds compiler warnings and must not be used alone.
- sdkconfig 与 build 被忽略；本地配置会保留旧值，修改 defaults 后需要核对实际生成配置。
  sdkconfig and build are ignored. Existing local values persist, so verify generated configuration after changing defaults.
- 日常本地测试沿用当前正式版本号，通过测试日期和说明区分；构建默认版本与 PICO_PROJECT_VER 的覆盖逻辑见根 CMakeLists.txt。
  Local tests retain the current published version and use dates/notes for identification; root CMakeLists.txt defines the fallback and PICO_PROJECT_VER override.
- 本地包不得覆盖已发布的固定下载文件；创建新 OTA 版本标签前先提交准确的更新日志，详见 AGENTS.md。
  Local bundles must not overwrite immutable published downloads. Commit accurate notes before new OTA tags as required by AGENTS.md.

GitHub CI 先构建以准备锁定的 IDF 组件，再用 Clang 执行宿主回归，保留 ASan/UBSan。已有 OTA 标签可通过 `Build and submit OTA firmware` 的手动入口输入原标签重试；构建与提交记录使用该标签的源码提交，不移动标签。失败时保留宿主测试日志附件。
GitHub CI builds first to prepare locked IDF components, then runs host regressions with Clang and ASan/UBSan. Retry an existing OTA tag through the manual entry of `Build and submit OTA firmware`; the build and submission use that tag's source commit without moving it. Failed host logs are retained as artifacts.

天气绘制回归需要 `fonttools==4.63.0` 和 `Pillow==12.2.0`，分别校验内置字体覆盖与背景位图。CI 在激活的 ESP-IDF Python 环境安装这两项测试依赖，本机运行回归也须在实际执行测试的 Python 环境中提供它们。
Weather raster regressions require `fonttools==4.63.0` and `Pillow==12.2.0` for built-in font coverage and background masks. CI installs these test dependencies in the activated ESP-IDF Python environment; local runs must provide them in the Python environment actually running the tests.

CI 等价命令 / CI-equivalent commands:

```sh
idf.py --preview -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci" set-target esp32s31
idf.py --preview -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci" build
python3 tools/run_reader_baseline.py
```

## 改动入口 / Where to work

| 任务 / Task | 入口 / Entry |
| --- | --- |
| 页面行为 / Page behavior | main/apps，对应 app_desc_t / Corresponding app_desc_t |
| 新增页面 / Add a page | main/CMakeLists.txt 与 main/app/app_registry.c / Build list and registry |
| 触摸、导航、睡眠前置 / Input, navigation, sleep handoff | main/app |
| 字体选择策略、USB 存储、文件任务交还与震动 / Font policy, USB storage, file handoff and haptics | main/services |
| 书源、进度、章节、排版 / Sources, progress, chapters, layout | main/book |
| 图片尺寸、解码与缩放 / Image probing, decoding, scaling | main/image |
| 公共控件与题头 / Shared controls and headings | main/ui |
| 字体加载与缓存 / Font loading and caches | main/font |
| 板级驱动 / Board support | components/board_support、device_power 及实际芯片组件 / Actual driver components |
| 显示、波形与灰阶 / Display, waveforms and gray normalization | main/display |
| 自动锁屏、密码与锁屏图缓存 / Idle lock, PIN and lock-frame cache | main/lock |
| 睡眠、唤醒与关机 / Sleep, wake and power-off | main/power |
| 设置存取与备份 / Settings storage and backup | main/settings |
| TF / 联网升级 / Card and online upgrades | main/ota |
| 时间服务与校时策略 / Time services and sync policy | main/time |
| 启动保护与恢复检查点 / Boot protection and resume checkpoints | main/boot |
| 阅读性能与资源诊断 / Reader timing and resource diagnostics | main/diagnostics |
| 平台输入适配 / Platform input adaptation | main/platform |

render 只绘图；副作用放在事件或生命周期回调。更改资源释放路径前先核对章节、字体、预绘和后台文件任务的所有权。具体契约见 AGENTS.md、app.h 和各模块头文件。
Render only paints; events/lifecycle callbacks own side effects. Check chapter, font, prefetch and background-file ownership before changing cleanup. See AGENTS.md, app.h and module headers.

## 测试与设备验证 / Tests and device acceptance

```sh
python3 tools/check_architecture.py
python3 tools/run_reader_baseline.py
```

[工具入口](../tools/README.md)列出宿主回归、资源生成和发布工具。[阅读性能文档](development/guides/READER_PERFORMANCE.md)说明诊断开关和测量方法。宿主回归保留 sanitizer；不得通过禁用检查掩盖失败。
The [tool guide](../tools/README.md) groups host tests, generators and release tools. The [reader performance guide](development/guides/READER_PERFORMANCE.md) documents diagnostic switches and measurements. Preserve host sanitizers instead of disabling checks to hide failures.

烧写前确认目标芯片、版本和生成包；使用该次构建生成的 flash_args，不手写或沿用旧板地址。设备操作应获得对应授权。构建通过、写入校验通过和用户实际验收是不同证据等级，报告时分别说明。
Before flashing, confirm the chip, version and bundle. Use that build's generated flash_args instead of handwritten or old-board addresses. Obtain the relevant device-operation authorization. Build success, flash verification and actual user acceptance are distinct evidence levels.

## Git、许可与文档 / Git, licenses and documentation

- 提交遵循[中文提交规范](COMMIT_CONVENTION.md)，不混入用户已有改动或本地配置。
  Follow the [Chinese commit convention](COMMIT_CONVENTION.md); exclude unrelated user changes and local configuration.
- 计划、调试与验收记录写入 docs/local 或忽略的构建目录。长期行为写入功能文档；用户可见变化按版本轻量记入 CHANGELOG。
  Keep plans/debug/acceptance records in docs/local or ignored build directories. Document lasting behavior in feature guides and user-visible changes briefly in CHANGELOG.
- 适用许可见根 LICENSE、CONTRIBUTING.md 与 THIRD_PARTY_NOTICES.md；保留既有授权和真实版权人，不批量更换文件头。
  See root LICENSE, CONTRIBUTING.md and THIRD_PARTY_NOTICES.md; retain existing grants and actual copyright holders without bulk header replacement.
- 旧芯片名不等于无效接口；生成表和第三方文件不要手工整理。以 AGENTS.md 的硬约束为准。
  Legacy chip names can be active APIs. Do not manually clean generated tables or third-party files; follow AGENTS.md's hard rules.
