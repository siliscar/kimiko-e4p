# 代码结构与依赖边界 / Architecture and dependency boundaries

本项目的产品目标是 Metalio E-INK4 Plus（ESP32-S31）。构建入口是根目录与 main 的 CMakeLists.txt；页面以 app_registry.c 和 app_desc_t 为准。
The product targets Metalio E-INK4 Plus (ESP32-S31). Root/main CMakeLists.txt define the build; app_registry.c and app_desc_t define pages.

最近静态核对：2026-11-04（Asia/Shanghai），范围为当前工作区的构建与页面入口、主要服务边界及持久化接口，不代表全功能重新验收。[功能全景](development/FEATURES.md)维护当前能力与证据边界，[开发入口](development/INDEX.md)维护资料权威。
Last statically checked on 2026-11-04 (Asia/Shanghai) for current build/page entries, major service boundaries and persistence interfaces, not complete functional reacceptance. The feature inventory owns current capability/evidence scope; the development index owns document authority.

## 系统与主路径 / System And Main Flows

- 用户通过触摸、盖板三键、电源键及可选 BLE 翻页器操作 684x1216 逻辑界面。硬件由 `components/board_support`、`device_power` 和芯片驱动持有，页面通过应用契约借用快照与显示状态。/ Users operate the logical interface through touch, three keys, power and optional BLE; board/drivers own hardware while pages borrow snapshots/display state.
- `touch_sampler` 在主循环阻塞时保留触摸帧；`app_loop` 对盖板三键逐帧执行同一按下/保持/抬手路由，不以队列末帧代替边沿。三键保持时长使用采样时间，排队不延长按住时间；切页请求先于新帧清理，弹窗、快捷层和页面取消边界保留原归属。/ The sampler retains touch frames while the loop blocks; panel keys share per-frame press/hold/release routing rather than only the final frame. Sample time excludes queueing delay from holds; stale route cleanup precedes new frames, retaining dialog, quick-layer and page cancellation ownership.
- FT6336U 检测阈值调校由触摸驱动持有：产品配置选择75%，100%恢复模组原值且不增加寄存器访问；降低时只写正常模式 `0x80`，复位/唤醒后重新计算、读回并在失败时验证恢复。它不是三键专用设置，不进入工厂/校准/升级模式，也不落 NVS；见[驱动契约](../components/cst836u/README.md)。/ The touch driver owns FT6336U threshold tuning: product configuration selects 75%, while 100% preserves the module default without extra register access. Reductions use only normal-mode `0x80`, recomputed after init/wake resets with verified writeback and rollback. It is not key-specific, enters no factory/calibration/upgrade mode and persists nothing in NVS; see the driver contract.
- `app_main` 完成设置、板级、前光、时间、VCOM 读取、开机图与字体装配，再经锁屏/恢复门控进入 `app_loop`。注册表共七项，前四项为主页，图片、组件与阅读手账为扩展页；传输与启动恢复虽然编译，不是额外根导航项。/ Boot assembles settings/hardware/light/time/VCOM/art/fonts before lock/resume gates and the loop; seven registry entries retain four root pages, with image/components/journal extensions and separate transfer/recovery entries.
- 开书经 `reader_open` 交还旧会话与预绘，`reader_chapter` 提交章节和布局，页面绘制后经 `reader_present`、刷新意图和物理显示出口呈现；失败保留相应回滚与错误语义。/ Open returns old session/prefetch resources; chapter commits precede painting and presentation through the existing refresh/physical exits, retaining rollback/error semantics.
- 页面退出、锁屏、USB 导出和关机先取消或等待持有资源的服务。`app_lifecycle` 协调天气、自动升级、校时、存储任务与无线交还；`storage_jobs` 统一文件消费者的交还，调用方仍负责画面和硬件恢复。/ Exit/lock/USB/off cancel or join owners; lifecycle coordinates weather/OTA/time/storage/radios, while callers retain painting and hardware recovery.
- 天气只经自建平台（`CONFIG_PICO_COMPONENT_API_ORIGIN`）的组件 GET 接口获取数据，未配置时默认停用；OTA 使用允许的 HTTPS 清单与镜像，源在 menuconfig 中配置，未配置时默认停用。浏览器传书由设备的 AP/STA 会话服务，不与天气共享后台所有权。/ Weather uses a self-hosted platform component GET API (`CONFIG_PICO_COMPONENT_API_ORIGIN`) and stays disabled until one is configured; OTA uses configured HTTPS feeds and mirrors, disabled until a feed is set. Browser transfer owns its separate AP/STA service session.

## 存储与外部边界 / Storage And External Boundaries

| 数据 / Data | 所有者与兼容要求 / Ownership And Compatibility |
| --- | --- |
| 系统与阅读偏好 / System and reader settings | `main/settings` 持有 NVS 与备份协议；原键、备份文件名及旧格式兼容独立于源码目录。/ Settings owns NVS/backup contracts; keys, filenames and compatibility do not follow source moves. |
| WiFi 凭据 / WiFi credentials | `wifi_transfer` 在 `rp_wifi/profiles` 的版本化 blob 保存最多八组，缺少新键时兼容旧 `credentials`；最近选择的网络为默认，公开快照不含密码。现有 TF 备份只导出默认网络；单网络恢复保留其他项，空记录清空全部。/ A versioned blob holds eight profiles with legacy fallback only when the new key is absent. The latest selection is preferred and public snapshots exclude passwords. Existing TF backups export only that network; restoring one retains others, while an empty record clears all. |
| 首页布局与天气 / Home and weather | `read_pico/home_regions` 一次保存上下布局并纳入 v33 备份；`kiiko_weather` 独立保存城市、偏好和有期限的缓存，不纳入设置/书籍备份。/ Atomic Home layout participates in v33 backup; standalone weather state/cache does not. |
| 阅读手账 / Reading journal | UI任务服务将当月逐日逐书明细与共享插画写入内置卷双快照，非格式化挂载、不依赖TF卡；不纳入设置/书籍备份，旧票根不补造明细。/ The UI-thread service owns internal-volume dual snapshots of current-month book/day rows and shared scenes, without formatting or SD dependency; excluded from backups and not synthesized from old tickets. |
| 书籍、字体、图片与缓存 / Content and caches | TF/内部书籍库由 `book_store` 与媒体/USB 服务交接；`.readpico` 缓存、进度、书签及资源借用保留既有标识。/ Content stores coordinate with media/USB; retain cache/progress/bookmark identities and borrowing contracts. |
| 恢复与升级 / Resume and updates | `main/boot` 保存恢复检查点；OTA 模块校验清单、镜像和槽位，Flash 写入与显示供电保护互斥。/ Boot owns resume checkpoints; OTA validates feeds/images/slots and coordinates flash writes with display guards. |
| 网站与平台 / Website and platform | 后端凭据、供应商许可、管理员发布、官网部署不由固件构建证明；不把草稿提交当成上线。/ Firmware builds do not establish credentials, provider permissions, administrator publication or deployment. |

## 目录职责 / Ownership

| 目录 / Directory | 职责与边界 / Ownership and limits |
| --- | --- |
| `main/app_main.c` | 启动装配，最后进入应用循环；根目录其余文件仅为构建配置。/ Boot assembly, then the app loop; other root files are build configuration only. |
| `main/boot/` | 启动保护、开书事务及深睡恢复检查点。/ Startup protection, book-open transactions and deep-sleep resume checkpoints. |
| `main/app/` | 页面契约、跨页请求接口、输入、导航、介质与生命周期调度。/ Page contracts, cross-page request APIs, input, navigation, media and lifecycle coordination. |
| `main/apps/` | 产品页面控制器及页面专用的目录、文件树视图。/ Product page controllers and their TOC/file-tree views. |
| `main/book/` | 书源、TXT/EPUB/MOBI、排版、阅读会话、书架规则、进度与封面缓存。/ Sources, TXT/EPUB/MOBI, layout, reader sessions, shelf rules, progress and cover caches. |
| `main/image/` | 通用图像探测、JPEG/PNG/静态 WebP 解码、裁剪与缩放，包含硬件 JPEG 适配。不得依赖书籍、UI、字体或页面。/ Generic image probing, JPEG/PNG/static WebP decoding, crop/scale and hardware JPEG adaptation; no book, UI, font or page dependencies. |
| `main/ui/` | 公共绘制、控件、交互反馈及题头样式；绘制接口不承担页面状态转换。/ Shared drawing, controls, feedback and heading styles; paint APIs do not own page transitions. |
| `main/font/` | 字体来源、加载、字形缓存与渲染。系统/阅读字体的选择策略在 services。/ Font sources, loading, glyph caches and rendering; services owns system/reader face policy. |
| `main/display/` | 统一刷新意图、波形目录、显示出口与几何、导航/阅读波形及灰阶归一；契约见 [DISPLAY_REFRESH.md](development/designs/DISPLAY_REFRESH.md)。/ Central refresh intents, waveform catalog, physical exits/geometry and navigation/reader/normalization waveforms; see the refresh contract. |
| `main/lock/` | 自动锁屏判定、锁屏图缓存与密码凭据及验证流程；睡眠交接由 power 负责。/ Idle-lock decisions, lock-frame caches, PIN credentials and verification; power owns sleep handoff. |
| `main/power/` | 锁屏进睡、唤醒、关机及 S31 CACHE_L1 恢复。/ Lock-to-sleep, wake, power-off and S31 CACHE_L1 restore. |
| `main/ota/` | TF/联网升级、发布清单、下载校验与升级任务。/ Card/online upgrades, release manifests, download verification and update workers. |
| `main/settings/` | NVS 设置、迁移与备份恢复，不含设置页面。/ NVS settings, migrations and backup/restore, separate from the settings page. |
| `main/time/` | 后台校时、对时准入策略及时间服务接口。/ Background calibration, sync admission policy and time-service contracts. |
| `main/diagnostics/` | 默认关闭的阅读计时与资源快照，不决定产品行为。/ Default-off reader timing and resource snapshots, without product behavior decisions. |
| `main/services/` | 跨页 USB 存储、字体上下文、文件任务交还、触摸震动、天气代理/缓存与阅读手账；见 [WEATHER.md](development/guides/WEATHER.md) 和 [READING_JOURNAL.md](development/guides/READING_JOURNAL.md)。/ Cross-page USB storage, font contexts, file-worker handoff, touch haptics, weather proxy/cache and reading journal; see their contracts. |
| `main/platform/` | S31 平台输入适配，向应用提供统一输入快照。/ S31 input adaptation with uniform application snapshots. |
| `main/assets/` | 内置字体、图标、图片等资源及生成数据。/ Bundled fonts, icons, images and generated assets. |
| `main/factory/` | 启动标定、硬件自检与 EPD 测试图案。/ Startup calibration, hardware self-tests and EPD test patterns. |
| `components/` | BSP、芯片驱动、可复用功能组件及第三方源码；不得包含 main 头文件。/ BSP, drivers, reusable components and third-party sources; no main headers. |
| `tools/` | 宿主回归、资源生成、诊断与发布工具；分类入口见该目录 README。/ Host tests, asset generators, diagnostics and release tools; see its README. |
| `docs/local/`、`build/` | 本地计划、日志、验收证据和构建产物，均不提交。/ Untracked plans, logs, acceptance evidence and build outputs. |

S31 浅睡由 `main/power/s31_sleep_compat.c` 限定 MPLL 唤醒校准的作用域；同目录 CMake 在旧 SDK 缺少 PSRAM 时钟交接时生成构建目录副本，不修改安装的 ESP-IDF。升级 SDK 时必须重新核对交接与校准次序；旧实现锚点缺失或重复时构建失败。外设、按键唤醒和 CACHE_L1 恢复仍由现有睡眠流程负责。仅本地验证可在配置时设置 `KIIKO_SLEEP_DIAG=1` 输出睡眠结果，普通构建不输出该周期日志。
`main/power/s31_sleep_compat.c` scopes the S31 MPLL wake-calibration workaround. The adjacent CMake file generates a build-local source copy when an older SDK lacks the PSRAM clock handoff, leaving the installed ESP-IDF intact. Recheck the handoff and calibration order when upgrading the SDK; missing or repeated legacy anchors fail configuration. The existing sleep flow still owns peripherals, key wake and CACHE_L1 restore. Set `KIIKO_SLEEP_DIAG=1` at configuration time only for local sleep-result logging; ordinary builds omit these periodic logs.

前光和无线停用后，板级睡眠入口关闭 P14 蓝牙／功放供电；P12 是触摸复位脚，保持高电平。唤醒时先由 `board_ioe_wake()` 恢复 P14，再恢复前光、触摸与加速度计。
After frontlight and radios stop, board sleep cuts the P14 Bluetooth/amplifier supply and keeps touch reset P12 high. Wake restores P14 through `board_ioe_wake()` before restoring frontlight, touch and accelerometer.

## 依赖规则 / Dependency rules

- 页面通过 app 中的公开契约协作，不包含另一个页面的实现。app 的调度代码不得包含 apps 目录的头文件。跨页请求仍由对应页面消费，不能借移动头文件改变生命周期。
  Pages collaborate through app contracts without including another page implementation. App coordinators must not include apps headers. The target page still consumes each cross-page request; moving a header does not transfer lifecycle ownership.
- 阅读页面只在成功开书、成功翻页和有效正文计时后通知手账服务；锁屏与离页尝试保存。`reading_journal_model`不读时钟或文件，`reading_journal`持有内存与存储，`ui_reading_journal`只绘制借用快照。日期、章节口径、轮换和容量上限见手账契约。
  Reading notifies the journal only after successful opens/turns and eligible body intervals, flushing on lock/exit. The model performs no clock/file IO, the service owns memory/storage and the UI paints borrowed views. The journal guide defines dates, chapters, scenes and limits.
- `book_shelf_category_state` 独占分类显示表、计数、稳定ID选择、两种布局的页码、本次开机各类的排序和分类菜单状态；页面只用API，不解引用内部字段。绑定已提交的 `book_collection` 时按ID保留页码/排序/选择，首次迁移按原目录来源匹配，分配失败不替换旧表；计数仅含当前可见且未隐藏的书。模块不访问UI、设置或存储。归类选择跨页/搜索/分类保留，旧文件批量管理仍沿用原清选规则。
  `book_shelf_category_state` owns display order/counts, stable-ID selection, both layout pages, per-category boot-session sorting and menu state behind its API. Binding preserves pages/sort/selection by ID, or legacy provenance on first migration; OOM retains the old table. Counts include only available, unhidden books. The module has no UI/settings/storage IO. Classification retains cross-page/search/category selections without changing legacy file-batch selection rules.
- `book_collection` 负责单层分类及一书一类的全路径归属，不调用文件移动、进度、收藏或手账接口。最多126类、每名24个Unicode字符、8192条路径；类别ID单调分配，删除/撤销不回收旧ID。每个TF库用只读CID身份隔离，内置库单独保存；索引在 `/flash/.kimiko-shelf-<identity>.0/.1`，保留暂时离线记录，同名文件不冒认。电脑改动路径后的自动重认不在契约内。
  `book_collection` owns single-level, single-category full-path assignments without file/progress/favorite/journal mutations. Limits are 126 categories, 24 Unicode name characters and 8192 paths; IDs are monotonic and not recycled by deletion/undo. Read-only card CID isolates each TF library and the builtin library; checked copies live at `/flash/.kimiko-shelf-<identity>.0/.1`, retaining offline paths without filename guessing. External path relocation is not automatically reconciled.
- 分类事务先克隆、写非活动副本、flush/fsync/close并回读校验，完成才发布内存状态；失败清理候选，清理失败阻断后续写入。新旧副本均损坏或任一副本发生I/O/内存读取错误时不清空覆盖；仅确定损坏时可从另一有效副本恢复。保存完成但显示表分配失败时报告“已保存”并要求重读，不把提交误报为失败。撤销只保存本次书架会话最近一次；索引尚未接入设置备份v33，独立导入导出流不代表备份集成。
  Transactions clone, write the inactive copy, flush/fsync/close and reread/validate before publishing memory. Failed candidates are removed; failed cleanup blocks writes. Two corrupt copies or any transient IO/OOM read error must not be silently overwritten; a demonstrably corrupt copy may recover from its valid peer. Post-commit display OOM reports a saved result and requests reload, not a failed transaction. Undo lasts one shelf session; standalone export/import is not integration into settings-backup v33.
- 完整扫描后只迁移一次旧一级目录（包括空目录），原名无效或超长时分配唯一短别名并保存原名来源；后续发现默认未分类，不按原目录改写归属。`book_category_ui`只管理列表/命名/顺序/确认/选书/筛选/全名查看并发命令，render无IO；`app_book`抬手后检查USB及库身份再执行，失败保留选择。独立选书封面复用既有单队列，整组准备且成功呈现才确认显示；离页/锁屏/拔卡取消未提交交互。
  Complete scans migrate legacy first-level folders once, including empty ones; invalid/long names get unique bounded aliases with retained provenance. Later discoveries default unclassified rather than deriving membership from folders. `book_category_ui` owns command state for lists/names/order/confirmations/picking/filters/full names, with pure rendering. After release, `app_book` checks USB/library identity and retains selections on failure. Picker covers share the existing single queue and acknowledge only successful grouped presentation; exits/lock/media loss cancel uncommitted work.
- `book_selection_ui.h` 共享多选控件、分页和底部操作的绘制/命中几何，不持有状态。书架多选使用独立内容布局并关闭底部导航命中及固定导航保护，退出后恢复普通布局；分类选书复用同一底栏。禁用文字只将4位读回值中的字迹转灰，不覆盖白底。
  `book_selection_ui.h` shares selection/pager/footer painting and hit bounds without state ownership. Shelf selection uses separate content geometry, disables navigation targets and fixed-navigation protection, then restores normal browsing on exit; category picking shares the footer. Disabled text tints only ink from 4-bit readback and preserves white pixels.
- `book_category_ops` / `book_path_state` 仅保留旧 `.kimiko-category-op` 物理整理日志的兼容恢复及故障测试，新归类不调用其移动/改名/删除接口。旧日志恢复仍要求TF挂载、USB未导出、HTTP/封面工作者交还；恢复原始路径记录与手账后才清源，坏日志阻断进一步旧整理及开书。该兼容层与新索引均不构成真实介质断电或设备行为验收；书签格式及现有读取/备份布局不变。
  `book_category_ops` / `book_path_state` remain for legacy physical-journal recovery and regressions only; logical classification never calls their move/rename/remove APIs. Recovery still requires mounted TF, USB inactive and returned HTTP/cover ownership, relocating raw state/journal before clearing source keys; ambiguous intent blocks further legacy writes/opens. Neither layer's host evidence proves device/power-loss behavior. Bookmark and existing backup layouts remain unchanged.
- `book_shelf_covers` 独占书架封面像素、任务代次、整组显示标记和预热进度；页面只通过 API 修改，同步只读借用槽位。普通离页取消任务并保留缓存；开书、换模式或介质失效先取消再释放。只有成功的整页或封面批次输出才能确认已显示，失败输出保留标记。它复用全局 `book_cover_jobs` 单队列，不提供多页面并行解码；导航、几何、设置、转换绘制和装饰缓存仍由页面负责。
  `book_shelf_covers` owns shelf pixels, job generations, grouped presentation markers and warm-up progress. Pages mutate through the API and borrow slots synchronously for reading. Ordinary exits cancel jobs while retaining artwork; opening a book, changing mode or losing media cancels before release. Only successful page or cover-batch output acknowledges artwork, so failures retain dirty markers. The module shares the single global `book_cover_jobs` queue rather than decoding multiple pages concurrently; pages still own navigation, geometry, settings, conversion, painting and decorative caches.
- `ui_reader_settings` 接收只读值快照，负责阅读设置控件、翻页模式示意和完成键的绘制；字号几何同时用于绘制与命中。页面保留面板壳、返回键反馈、设置读取/保存、点击处理、重排和刷新调度。模块不直接依赖页面、book 或系统设置，不持有快照、字体或帧缓冲；通用开关由字体与按键面板复用。
  `ui_reader_settings` paints reading-setting controls, turn-zone diagrams and Done from a read-only value snapshot, with scaled geometry shared by drawing and hit testing. The page retains sheet shells, Back feedback, settings reads/writes, actions, reflow and presentation scheduling. The module does not directly depend on pages, book modules or system settings and retains no snapshot, font or framebuffer; font and key sheets reuse its switch painter.
- MOBI 通过 `book_source` 的同一会话接入共享 HTML/排版；`book_metadata` 分派 EPUB/MOBI 轻量元数据及封面，独立于活动阅读句柄。`mobi_codec` 只含有界 PalmDOC 解压，不依赖 main；原始坐标、缓存兼容及限制见 [MOBI 契约](development/guides/MOBI.md)。
  MOBI uses the same source/session and HTML/layout contracts; `book_metadata` dispatches independent EPUB/MOBI metadata/covers. `mobi_codec` is a main-independent bounded PalmDOC decoder. See the MOBI contract for coordinates, caches and limits.
- 书籍封面依赖 image；壁纸、阅读背景和图片页直接依赖 image。book_cover.h 暂时转出原有图像声明以兼容既有调用方，新增通用图像调用应直接使用 image_decode.h。
  Book covers depend on image; wallpaper, reader backgrounds and image pages use image directly. book_cover.h forwards legacy image declarations for compatibility; new generic consumers should include image_decode.h.
- `components/webp_decoder` 保留 libwebp 1.6.0 的可移植解码子集和原许可，PSRAM 分配适配不改上游源码。`image_webp` 限制源像素和一屏输出、合成白底灰阶；上游互斥保护初始化，不开解码工作线程。EPUB 层解析书内路径与样式表相对 URL，HTML 层只将 `body` 的单图背景转为页首插图；不扩展为浏览器背景渲染，也不改变用户阅读背景。独立图片文件入口仍只列出 JPG/PNG。
  `webp_decoder` retains the portable libwebp 1.6.0 subset and licenses, with external PSRAM allocation adapters. `image_webp` bounds source pixels/panel output and composites onto white; mutexes protect initialization without decoding workers. EPUB owns archive paths and stylesheet-relative URLs; HTML maps only a body's single-image background to a leading illustration, without browser background rendering or reading-background changes. Standalone image pickers still list only JPG/PNG.
- main 的头文件搜索路径为组件私有。目录边界由结构检查补充约束，不表示每个目录都是独立 IDF 组件。
  Main header search paths are component-private. Structural checks enforce directory boundaries; directories are not automatically separate IDF components.
- main 根目录只保留 app_main.c、CMakeLists.txt、Kconfig.projbuild 和 idf_component.yml；新增功能文件必须放入最接近的职责目录，结构检查会拒绝散落在根目录的功能文件。
  Main root contains only app_main.c, CMakeLists.txt, Kconfig.projbuild and idf_component.yml. New feature files belong in the nearest ownership directory; structural checks reject root-level feature files.
- 头文件短名保持兼容，由 CMakeLists.txt 显式登记组件私有搜索路径；跨目录相对包含必须指向真实文件。目录迁移需同步宿主测试和工具的源码路径，历史 Git 对照仍使用历史提交里的路径。
  Short header names stay compatible through explicit component-private search paths in CMakeLists.txt. Relative includes across directories must resolve to real files. Moves also update host-test/tool source paths while historical Git comparisons retain historical paths.
- 资源所有权是接口的一部分：预绘交还后才能释放字体/章节；USB 与文件工作者交还存储后才能重挂。移动文件时保留取消、失败和重试路径。
  Ownership is part of the API: return prefetch borrows before freeing fonts/chapters, and return USB/worker storage ownership before remounting. Moves preserve cancellation, failure and retry paths.

## 构建与验证 / Build and verification

```sh
python3 tools/check_architecture.py
python3 tools/run_reader_baseline.py
idf.py --preview build
```

结构检查覆盖根目录归属、跨目录头文件依赖、私有头搜索路径、产品源文件登记和既有测试入口；阅读回归执行真实模块及对应宿主替身，并保留 ASan/UBSan。构建和宿主测试不能替代设备显示、睡眠、触摸与存储验收。
Structural checks cover root ownership, header boundaries and private search paths, product source registration and existing test entry points. Reader regressions run real modules with host doubles and ASan/UBSan. Builds and host tests do not replace device display, sleep, input or storage acceptance.

GitHub 构建入口为 `.github/workflows/build.yml`，正式标签的同源固件、交付物与 OTA 草稿入口为 `.github/workflows/ota-submit.yml`。发布授权、Release 命名、不可覆盖附件和官网/TF/OTA 一致性以 AGENTS 为准；本地治理不触发这些操作。
GitHub build and official-tag artifacts/OTA drafts enter through the build and OTA workflows. AGENTS governs publication authority, naming, immutable assets and website/TF/OTA consistency; local governance triggers none of them.

旧移植评估、rc68/rc69 和旧官网维护资料保留当时事实，不覆盖当前 S31 源码。本 checkout 不含旧 `flash/` 站点目录；仍在仓库的说明书输入和生成器不代表站点可直接构建或已经部署。具体能力、证据缺口与适用测试见功能全景。
Historical port/S3/site documents do not override current S31 code. This checkout lacks the old flash-site directory; retained manual inputs/generators do not establish a buildable or deployed site. See the feature inventory for capability/evidence gaps.

## 兼容与第三方代码 / Compatibility and third-party code

本地组件按职责命名：`board_support` 提供 `board_*` 板级入口，`device_power` 提供电源接口，`book_search` 提供离线搜索与输入候选，`wifi_transfer` 提供网络传书。新增文件与接口沿用职责名，旧板实现用硬件后缀区分。构建配置通过 `components/board_support/sdkconfig.rename` 接续旧值。
Local components use role names: `board_support` exposes `board_*` board entry points, `device_power` owns power interfaces, `book_search` owns offline search and input candidates, and `wifi_transfer` owns network transfers. New files and APIs follow these roles; old hardware backends carry hardware suffixes. `components/board_support/sdkconfig.rename` preserves existing configuration values.

`Read_Pico` 固件工程身份、NVS 名称、`.readpico` 卡上缓存目录、备份格式、已发布地址及既有日志标识属于兼容边界；上游来源网址、许可、协议文档和证明文件保留真实身份。内部代码改名不迁移这些外部标识。
The `Read_Pico` firmware identity, NVS names, `.readpico` card caches, backup formats, published URLs and existing log tags are compatibility boundaries. Upstream source URLs, licenses, protocol documents and provenance retain their real identities. Internal renaming does not migrate these external identifiers.

board_*、cst836u_*、fca9555_* 包含当前 S31 使用的兼容入口；名称不代表代码已失效。通用图像模块也保留 book_image_* 等原有函数名。删除前需核对构建、注册、实际消费者与宿主工具。
board_*, cst836u_* and fca9555_* include active S31 compatibility interfaces. Generic image code also retains book_image_* API names. Check the build, registration, actual consumers and host tools before deletion.

第三方组件保留其许可和必要测试/示例；不能按目录大小判断它们都进入产品固件。生成字体、图标与波形表按各自生成流程维护。
Third-party components retain licenses and needed tests/examples. Directory size does not indicate what is linked into firmware. Maintain generated fonts, icons and waveform tables through their generators.
