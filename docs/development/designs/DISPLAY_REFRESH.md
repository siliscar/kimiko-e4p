# 显示刷新契约 / Display refresh contract

页面和控件提交“刷新意图 + 区域 + 画面属性”，显示层决定波形、驱动模式和像素覆盖。绘制、输入、页面状态和计数仍归原模块；接口同步执行，不排队、不安排延迟补刷。原有整页导航、强刷、图片清白和错相翻页保留语义出口。
Pages and controls submit intent, bounds and frame attributes. Display selects waveforms, drive modes and pixel coverage. Original modules retain painting, input, page state and counters. Calls execute synchronously without queues or deferred passes. Semantic exits remain for navigation, full refresh, image whitening and staggered turns.

## 分层 / Ownership

| 文件 / File | 职责 / Responsibility |
| --- | --- |
| `main/display/display_refresh.h` | 页面唯一的通用局部/整屏意图接口。/ Public area/screen intent interface. |
| `display_refresh_policy.c` | 纯解析：意图和属性 → 波形编号、模式、覆盖、脏区膨胀。/ Pure intent/attribute → waveform ID, mode, coverage and dilation. |
| `display_refresh_context.c` | 每次同步读取现有主页与背景设置，不缓存状态。/ Read existing main/background settings for each call without caching. |
| `display_refresh.c` | 解析后只调用一次既有物理出口，原样返回错误。/ Resolve and invoke exactly one existing physical exit, returning its error. |
| `display_waveform_catalog.c` | 23 个稳定编号、来源、主要基底、派生说明和用途；惰性调用原构造器。/ 23 stable IDs with sources, primary bases, recipes and roles; resolve original builders lazily. |
| `display.c` | 物理执行、导航多段输出、清屏计数、扫描档、OTA 互斥、供电准入与失败恢复。/ Physical execution, staged navigation, counters, scan profiles, OTA exclusion, power admission and recovery. |
| `display_main_waveform.c` / `display_reader_waveform.c` / `display_normalize.c` | 原有派生构造与分配失败回退，不属于页面接口。/ Existing derivation builders and allocation-failure fallbacks, outside the page API. |
| `components/e0470_epaper_waveform/` | 原始表、基础裁剪及基础短表；生成/供应商表不手改。/ Source tables, base trims and short tables; do not hand-edit generated/vendor data. |

`display_refresh_area()` 的面积由调用方给出，策略不扩大矩形；有膨胀的意图交给原出口扩大 dirty masks。`display_refresh_screen()` 支持 AUTO/DIFF 且无膨胀的策略；不支持的组合在构造波形前拒绝。整屏范围不代表全像素驱动。
Callers own the rectangle passed to `display_refresh_area()`. Policy does not enlarge it; padded intents let the existing exit dilate dirty masks. `display_refresh_screen()` accepts unpadded AUTO/DIFF policies, rejecting unsupported combinations before waveform construction. Screen bounds do not imply full-pixel drive.

## 意图矩阵 / Intent matrix

AUTO 保留旧出口行为：区域 GL16 会全像素驱动，DU 按原差分；整屏走原 high-level 路径。DIFF 只驱动变化像素；FULL 强制区域全像素。`QUICK` 仅表示已选短刷新状态，不能让调用方自由指定波形。
AUTO retains the old exit behavior: area GL16 drives all pixels, DU uses the original differential, and screen requests use the original high-level path. DIFF drives changed pixels; FULL forces all pixels in the region. QUICK conveys an already-selected short-refresh state without allowing arbitrary waveforms.

| 意图 / Intent | 波形 / Waveform | 模式 / Mode | 覆盖 / Coverage |
| --- | --- | --- | --- |
| DETAIL | default | GL16 | DIFF |
| LAYOUT | default | GL16 | AUTO |
| SETTLE / OVERLAY | default | GL16 | FULL |
| FEEDBACK | follow | DU | DIFF |
| MOTION | follow | DU | AUTO |
| READER_FOOTER | follow | DU | AUTO |
| DYNAMIC | default | APP_DYNAMIC_REFRESH_MODE | AUTO |
| MAIN_CLOCK | 主页快刷 main-bw，否则 main-gray / fast main-bw, otherwise main-gray | DU / GL16 | DIFF |
| MAIN_DETAIL | 普通 main-normal，否则 main-gray / ordinary main-normal, otherwise main-gray | GL16 | DIFF |
| MAIN_SCROLL | 普通 main-normal，否则 default / ordinary main-normal, otherwise default | GL16 | DIFF，1 行 + 1 列字节膨胀 / 1 row + 1 column byte padding |
| READER_TEXT | reader-text | GL16 | DIFF |
| READER_BODY | IMAGE 优先 reader-image；否则背景 reader-background / 无背景 reader-ink / IMAGE wins, otherwise background reader-background or reader-ink | QUICK DU，否则 GL16 / QUICK DU, otherwise GL16 | DIFF |
| READER_LAYOUT | 背景 reader-background，否则 reader-ink / background reader-background, otherwise reader-ink | QUICK DU，否则 GL16 / QUICK DU, otherwise GL16 | AUTO |
| READER_IMAGE | full | GC16 | AUTO |
| READER_CLEANUP | full | GC16 | FULL |
| RESTORE | HOLD_GRAY 为 normalize-gray，否则 normalize / HOLD_GRAY normalize-gray, otherwise normalize | GL16 | FULL |
| CAPSULE | default | DU | DIFF，1 行 + 1 列字节膨胀 / 1 row + 1 column byte padding |

仅 READER_BODY 接受 QUICK/IMAGE，READER_LAYOUT 接受 QUICK，RESTORE 接受 HOLD_GRAY；其余意图只接受 DEFAULT。无效意图、无效标志或整屏组合返回 `EPD_DRAW_INVALID_CROP`，缺表返回 `EPD_DRAW_NO_PHASES_AVAILABLE`；都不扫描。
Only READER_BODY accepts QUICK/IMAGE, READER_LAYOUT accepts QUICK, and RESTORE accepts HOLD_GRAY. Other intents accept DEFAULT only. Invalid intents, flags or screen combinations return EPD_DRAW_INVALID_CROP; missing tables return EPD_DRAW_NO_PHASES_AVAILABLE. Neither scans.

## 阅读状态与输出顺序 / Reader status and output order

阅读正文静置时，分钟、电量及充电变化不单独触发顶部刷新；下一次正文翻页或整页重绘显示当前状态。窗口化页眉与系统状态栏共用80/160像素顶部带，全屏隐藏状态栏时不刷新顶部；主页面和快捷栏仍按原规则更新状态。
Minute, battery and charging changes do not independently refresh a resting reader. The next body turn or whole-page redraw shows current status. Windowed headers and system status share the 80/160-pixel top band; full screen with a hidden status bar has no top output. Main pages and the quick layer retain their status rules.

局部翻页优先输出正文，成功后再输出窗口化页脚或全屏进度，最后更新有变化的顶部。为缩短顶底栏刷新，恢复旧路径：顶部与全屏进度使用 DETAIL 默认裁剪 GL16（37相、白到黑13推），普通页脚使用 READER_FOOTER 短 DU（8相、白到黑7推）；换章或插图帧的页脚使用 DETAIL GL16 清理。顶底栏不随阅读背景切换波形。插图正文保留原灰阶出口，水波纹正文保留原表和节拍。任一步失败都短路并保留错误，只有成功完成相应输出才确认顶部状态。手动/周期GC16、工具带和关闭面板归一仍保留原出口，不额外叠加顶底栏。
Partial turns present the body first, then the windowed footer or full-screen progress after success, and changed status last. To shorten chrome output, restore the legacy paths: status and full-screen progress use DETAIL trimmed GL16 (37 phases, 13 white-to-black pushes); ordinary footers use READER_FOOTER short DU (8 phases, 7 white-to-black pushes), while chapter and illustration-frame footers use DETAIL GL16 cleanup. Chrome waveforms do not depend on reading backgrounds. Illustration bodies retain their grayscale exit, and ripple bodies retain their tables and pacing. Any error stops later output and propagates; status acknowledgement requires successful presentation. Manual/periodic GC16, tools bands and panel-close normalization retain their exits without added chrome passes.

静置不单刷顶部的策略针对用户观察到的顶部局刷后正文变浅；顶底栏按用户速度反馈恢复旧波形选择，没有修改VCOM、供电或波形数据。宿主回归只证明刷新时机、顺序和错误处理，累计墨度、残影与实际耗时须由实屏验证。
Deferring idle status output addresses reported body fading after top-band updates; the legacy chrome waveform choices return in response to latency feedback, without changing VCOM, power settings or waveform data. Host regressions establish timing, order and error handling; accumulated ink density, ghosting and actual latency require panel verification.

## 界面三色 / UI palette

界面底色和控件目标统一为白、黑及固定灰（`UI_GRAY_LIGHT=0x90`，4 位灰阶第 9 档），不随系统对比度漂移。卡片和导航以白底为主，文字使用黑色或明确的反白；固定灰用于进度底槽、未启用控件及活动状态。边框使用黑色，底栏分隔线使用固定灰0x90。共享底栏只有四枚图标与选中横条，高104像素，图标垂直居中；内容扫描止于1088，2像素0x90灰分隔线位于1112，保留24行白色保护带。
UI surfaces and controls target white, black and one fixed gray (`UI_GRAY_LIGHT=0x90`, level 9 in the 4-bit framebuffer), independent of system contrast. Cards and navigation favor white, text uses black or explicit white, and fixed gray marks tracks, disabled controls and active states. Frames are black and the footer divider uses fixed gray 0x90. The shared footer contains four icons and a selection marker in 104 pixels with vertically centered icons; content scans end at 1088, and a two-pixel 0x90 gray divider at 1112 retains a 24-row white guard.

三色约束发生在控件绘制入口，不对整帧量化。封面、图片、头像、时钟图像、阅读正文背景及字体和图标抗锯齿保留原路径；书架亚克力挡板保留原来的模糊、灰阶与透明合成，快刷模式原有的点阵处理继续生效。需要保留灰阶的绘制使用原始接口，不能经过控件调色板。
Apply the palette at control drawing entry points rather than quantizing the finished frame. Covers, artwork, avatars, clock artwork, reading backgrounds and font/icon antialiasing retain their existing paths. Acrylic shelf guards retain blur, tones and translucency, including the existing fast-mode dither. Drawing that must retain gray uses raw interfaces rather than the control palette.

快捷栏使用不透明白底；前光胶囊为黑底白填充，图标位于固定白色圆盘内。拖动和松手走 CAPSULE 的完整 DU 差分（复用 default，20相），脏掩模外扩1行和1列字节，不追加松手定稿。展开覆盖新旧层高的并集；收起重画底页后，通过 RESTORE 整屏全像素归一。归一表复用既有37相，对最终黑色像素统一18相擦白及18相推黑；只有当前显示阅读背景正文时才使用 HOLD_GRAY 保护未变化的图片灰阶，其他页面完整归一。退出阅读到主页面，包括同页退回书架，使用整屏GC16。刷新失败保留可靠全刷恢复。三色与软件验证不保证光学无残影，须实屏验证累计拖动、开合、图片与背景正文。
The quick layer uses opaque white; frontlight capsules use black tracks, white fills and icons on fixed white disks. Dragging and release use CAPSULE complete DU differences (the existing 20-phase default table), with one row and one column byte of dirty-mask padding and no release settle pass. Opening covers the union of old/new heights. Closing repaints the underlay and uses RESTORE for whole-screen full-pixel normalization. The existing 37-phase table gives final black pixels the same 18 erase and 18 black pushes; only a visible reading body with a background requests HOLD_GRAY to protect unchanged picture grays, while other pages use full normalization. Leaving the reader for a main page, including the shelf in the same app, uses whole-screen GC16. Failed output retains reliable full recovery. Neither the palette nor software checks prove optical ghost removal; verify repeated drags, open/close, images and background reading on the panel.

## 波形目录与依赖 / Catalog and dependencies

| 组 / Group | 编号对应的诊断名 / Diagnostic names |
| --- | --- |
| 基础 / Base (9) | default, full, gray8, follow, text-base, capsule-drag, capsule-white, capsule-gray, apply |
| 主页 / Main (7) | main-gray, shelf-exit, main-water, main-bw, main-normal, shelf-gl16, shelf-gc16 |
| 阅读 / Reader (5) | reader-text, reader-ink, reader-water, reader-background, reader-image |
| 归一 / Normalize (2) | normalize, normalize-gray |

gray8、capsule-white、capsule-gray 是保留的备用表；apply 是错相逐带执行壳。目录不因为无当前产品调用而删除表，也不合并光学目的不同的派生表。
gray8, capsule-white and capsule-gray are reserved; apply is the staggered-band execution shell. The catalog neither removes unused product alternatives nor merges derivatives with different optical purposes.

`parent` 是首要基底，并非完整依赖图。阅读文字与背景派生会借用主页水波纹的白列；归一以 reader-ink 为基底。无背景的 reader-water 与启用背景时的 reader-background 分别登记，目录 getter 不隐式读取背景设置。原低层兼容 getter 仍按原设置和回退规则工作。
The parent field identifies the primary base, not every dependency. Reader text/background derivatives borrow the main-water whitening row; normalize builds on reader-ink. Background-independent reader-water and reader-background have separate IDs. Catalog getters do not implicitly read background settings. Existing low-level compatibility getters retain their original settings and fallback rules.

描述查询不分配、不初始化。get/inspect 可能触发原有惰性构造；分配失败或缺模式时保留原构造器的回退。inspect 统计实际返回表的相数、相等灰度对驱动和黑白端点推动次数，不能证明拿到了描述中期望的派生表，也不能据此推断光学效果。未被选用的阅读/归一表不再因页面预选指针而提前构造，分配时机可能晚于旧实现。
Descriptor queries neither allocate nor initialize. get/inspect may trigger existing lazy builders and preserve their allocation/missing-mode fallbacks. inspect reports the resolved table's phases, equal-gray activity and endpoint pushes; it does not prove the intended derivative was constructed or establish optical quality. Unselected reader/normalize tables no longer build merely because a page preselected a pointer, so allocation can occur later than before.

当前基础波形只有一个温度档，显示出口固定传入 25°C，目录检查与原 getter 一样读取第一个档；它没有新增温度传感器或温度补偿。未来引入多温度档必须同时修改选择与检查契约。
Current base waveforms have one temperature range. Display passes a fixed 25°C, and catalog inspection reads the first range like the existing getter. This adds neither sensing nor compensation. Multiple ranges require coordinated selection and inspection changes.

## 清理周期与扫描 / Cleanup and scan timing

`display_refresh_config.h` 集中既有阈值：系统整页 40 次、主页内容 12 次、非普通子页每 3 次全像素 GL16；阅读缺省 15 页，仍支持原来的 0/5/10/15/30 设置。局部按钮/弹窗不累计系统整页清理；正文计数、手动清理和成功提交规则仍在原模块。
display_refresh_config.h centralizes existing thresholds: 40 system pages, 12 main-content updates, and full-pixel GL16 every third nonordinary subpage. Reader defaults to 15 turns and retains 0/5/10/15/30 settings. Local controls/dialogs do not advance the system-page cadence. Original modules retain reader counters, manual cleanup and successful-commit rules.

FOLLOW DU 与胶囊拖动选择 FAST 扫描档，其余保留 FULL。FAST/FULL 是时序配置名，不能直接推断物理帧时长。S31 默认 20 MHz、8-bit 总线下，两档当前均求解为 16 μs/行、11,008 μs/相；这是板级公式结果，不是实测刷新耗时。错相翻页还要计入带数、错相节拍、计算与供电开销。
FOLLOW DU and capsule drag select FAST; other outputs retain FULL. These are timing profile names, not latency guarantees. With the current S31 20 MHz 8-bit bus, both solve to 16 microseconds per line and 11,008 per phase. These are timing calculations, not measured refresh latency. Staggered turns also include band offsets, ticks, computation and power overhead.

OTA/display 递归互斥、供电检查、成功后提交 back buffer、失败后重建真实基准、欠载降频恢复继续由原物理出口负责。新策略不绕过这些出口，不提前确认一帧成功。
Existing physical exits retain recursive OTA/display exclusion, power checks, success-only back-buffer commits, baseline reconstruction after failures and underrun frequency fallback. Policy never bypasses these exits or confirms success early.

## 维护与验证 / Maintenance and verification

新增场景先增加意图/属性和固定期望测试，再迁移调用方。新增波形登记来源、用途、基底、构造器回退与实屏依据。产品层禁止直接引用表、派生 getter 或带波形参数的原始出口，也禁止包含内部策略/目录头；工厂 VCOM 路径保留原有低层例外。
Add an intent/attribute and fixed expected tests before migrating a new scene. Register each waveform's source, purpose, base, fallback and panel evidence. Product layers must not reference tables, derivative getters, raw waveform-taking exits or internal policy/catalog headers. Factory VCOM calibration retains its original low-level exception.

```sh
python3 tools/run_reader_baseline.py refresh present quick loop lock navigation display ripple architecture
python3 tools/run_reader_baseline.py
idf.py --preview build
```

refresh 专项覆盖 23 表身份与实际驱动统计、1,152 组策略输入、唯一出口、面积/膨胀、错误透传、无效请求和页面呈现边界。结构检查包含拒绝反例。测试中的目录/硬件替身与真实策略分开，阅读/快捷栏原有输出断言继续有效。
The refresh suite covers 23 table identities/statistics, 1,152 policy inputs, one-exit dispatch, bounds/padding, errors, rejection and page presentation boundaries. Structural checks include rejecting fixtures. Catalog/hardware doubles remain separate from real policy, retaining existing reader/quick-layer output assertions.

波形合并或改参数前，用同一设备、温度、VCOM、电源和固定图案，对比黑白文字互换、16 灰阶、背景正文、插图、时钟、输入候选、快捷栏开合。记录残影、白底回灰、字芯黑度、闪烁及实测延迟，包含多次累积和睡眠/失败恢复。宿主与编译结果不能代替这一步。
Before merging or retuning waveforms, use the same device, temperature, VCOM, power and fixed patterns to compare black/white text reversals, 16 grays, background text, illustrations, clocks, input candidates and overlay open/close. Record ghosting, white drift, glyph darkness, flashing and measured latency, including repeated accumulation and sleep/failure recovery. Host tests and compilation do not replace this step.

## 高压生命周期 / High-voltage lifecycle

高压生命周期由 epdiy 底层统一管理。产品显示出口只在真实扫描前上电；帧缓冲绘制、图片解码、波形准备、无变化返回均不提前上电。完整显示组通过 `display_present_begin/end()` 配对，最外层结束即主动关电；阅读正文、页脚和顶部属于同一组，内部子输出不逐段关电。显示组可以嵌套，但不会持供电锁等待准备工作，也不会延长空闲期限。
The epdiy layer owns the HV lifecycle. Product display paths power on only before physical scans; framebuffer drawing, image decoding, waveform preparation and unchanged updates do not power on early. Pair complete display groups with `display_present_begin/end()`; the outermost end immediately requests shutdown. Reader body, footer and header share one group without power cycling at each sub-output. Groups may nest but neither hold the power mutex during preparation nor extend idle deadlines.

独立的微秒绝对定时器唤醒高优先级任务，在最后一次物理扫描结束后100ms发起空闲关断，作为显示组内停顿或直接使用底层输出时的兜底。普通更新、清屏盲推、开机、锁屏和出厂测试共用这条路径；应用循环、图片解码、书籍加载和OTA显示锁不参与计时。独立显式上电而没有扫描也有100ms期限；重复上电请求、无变化刷新、失败返回和过期通知不延后已有期限。
An absolute microsecond timer wakes an independent high-priority task to request shutdown 100 ms after the last physical scan, covering pauses inside groups and direct low-level output. Normal updates, blind clearing pulses, boot, lock screens and factory tests share this fallback. The application loop, image decoding, book loading and OTA display lock do not own the deadline. Explicit standalone power-on without a scan also expires after 100 ms; repeated on requests, unchanged updates, failed draws and stale notifications do not extend an existing deadline.

实际扫描与关断共用专用递归锁，扫描进行期间不截断波形。下一次扫描在锁内重新确认供电；扫描结束立即通过GPIO撤MODE/XOE，再开始空闲计时。关断命令失败时保留“可能带电”状态，5ms后重试；成功仅表示扩展口接受了关断写入。关断未确认时开始新刷新，须先确认关闭并重新完整上电，不复用不确定供电。马达与供电位使用原子状态，获得I2C事务锁后才形成OUT0快照，防止排队的马达写入恢复过期供电位。
Physical scanning and shutdown share a dedicated recursive mutex so active waveforms are never cut off. Each new scan ensures power under that lock; completion immediately lowers MODE/XOE through GPIO and starts idle timing. An unconfirmed shutdown retains the possibly-powered state and retries after 5 ms. Success means the expander accepted the shutdown write. A new scan after unconfirmed shutdown must confirm off and fully repower, rather than reuse uncertain power. Motor and power bits use atomic state, and OUT0 is sampled only after taking the I2C transaction lock so queued motor writes cannot restore old power bits.

板级关断顺序为：GPIO拉低XOE/MODE，等待至少12µs，再发布并写入VCOM/PWRUP关闭位，等待原有2ms，最后关闭WAKEUP。供电关闭位在12µs等待后才可被马达等共享扩展口写入者看到，避免并发写入提前断轨。VCOM参数和波形数据不由该机制修改。
Board shutdown lowers XOE/MODE through GPIO, waits at least 12 µs, publishes and writes VCOM/PWRUP off, waits the existing 2 ms, then disables WAKEUP. Shared expander writers such as the motor see the off bits only after the 12 µs wait, preventing concurrent writes from disabling rails early. This mechanism does not alter VCOM parameters or waveform data.

设置模块写事务及联网/TF OTA在Flash写入前调用 `epd_flash_begin()`：确认关电后持供电锁直到 `epd_flash_end()`，期间新扫描等待。确认失败则禁止写入并返回失败；仅成功的begin需要同任务配对end。锁顺序固定为显示/OTA锁→供电锁→I2C，不得持供电锁再请求显示锁。初始化前允许调用，但屏幕初始化/销毁必须与写入串行；保护不覆盖所有其他模块的NVS或系统内部Flash写入。
Settings write transactions and online/card OTA call `epd_flash_begin()` before flash access: confirm shutdown and hold the power mutex until `epd_flash_end()`, blocking new scans meanwhile. Failed confirmation forbids writing and returns failure; only a successful begin is paired with end on the same task. Lock order is display/OTA -> power -> I2C; never request the display lock while holding the power mutex. Calls before display initialization are allowed, but initialization/teardown must serialize with writes. This protection does not cover every other module's NVS or internal system flash writes.

100ms是空闲关断的软件期限，不能等同于实际高压已在100.000ms降到零。RTOS调度、共享I2C占用、总线故障和未接入保护的Flash操作仍可能造成延迟；诊断开关 DISPLAY_POWER_DIAG 记录期限、处理开始、完成和延迟，显示组提前关电时延迟可以为负值，均不能替代引脚或电压测量。若需要故障条件下也有严格电气上限，现有I2C扩展口控制路径需要独立硬件关断支持。
The 100 ms idle deadline is a software limit, not proof that physical voltage reaches zero at exactly 100.000 ms. RTOS scheduling, shared I2C contention, bus faults and unguarded flash operations can still delay it. DISPLAY_POWER_DIAG records deadlines, processing start, completion and lateness; early group shutdown can produce negative lateness. Logs do not replace pin or voltage measurements. A strict electrical limit under faults requires independent hardware shutdown support beyond the present I2C expander path.
