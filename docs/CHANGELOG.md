# 版本变更 / Changelog

按版本简述对用户可见的变化；使用方法见 [README](../README.zh-CN.md)。
User-visible changes by release; see [README](../README.md) for usage.

## 未发布 · 天气改用 Open-Meteo / Unreleased · Weather moves to Open-Meteo

- 天气改为直接使用公开的 Open-Meteo：城市搜索走地理编码接口、预报走 `/v1/forecast`，不需要 API 密钥，也不再依赖任何自建平台。原 `CONFIG_PICO_COMPONENT_API_ORIGIN` 代理协议移除。/ Weather now uses public Open-Meteo directly: city search via the geocoding API and forecasts via `/v1/forecast`, with no API key and no self-hosted platform. The old `CONFIG_PICO_COMPONENT_API_ORIGIN` proxy protocol is removed.
- 只保留中国大陆结果，时区统一归一到 UTC+8；城市 ID 形如 `OM1808926`，普通刷新不重新定位。/ Only mainland-China hits are kept, every timezone normalises to UTC+8, city ids look like `OM1808926`, and normal refreshes never relocate a city.
- WMO 天气码在设备内映射为原界面使用的和风兼容码与中文描述，背景与图标不变。/ WMO codes are mapped on-device to the QWeather-compatible codes and Chinese labels the existing UI already uses, keeping backgrounds and icons unchanged.
- 「自动定位」移除：不再引入 IP 归属地第三方服务，没有城市时请手动搜索。/ Auto-locate is removed: no third-party IP-geolocation is introduced, so pick a city manually when none is set.
- 有效期在设备本地合成（1 小时新鲜、2 小时失效），替代代理返回的时间窗。/ Validity is synthesised on-device (fresh for one hour, expired after two) instead of the proxy-provided window.
- 「定位」恢复可用：向 `myip.ipip.net` 询问出口 IP 所属城市，剥掉「市/省」后缀后走地理编码取坐标；每次开机最多自动尝试一次，失败仍提示手动选城。/ Locate works again: the device asks `myip.ipip.net` which city the egress IP belongs to, trims the 市/省 suffix and geocodes it, at most once per boot; failures still ask for a manual choice.
- 城市搜索在首查为空时补「市」重查一次，地级及以上城市覆盖率由 73% 提升到 89.6%。/ City search retries once with a 市 suffix when the first pass is empty, lifting prefecture-level coverage from 73% to 89.6%.
- 九宫格/全键盘词库补入 293 个中国地级及以上城市，输入 `hangzhou` 直接出「杭州」，不再需要先选「杭」再选「州」；同音城市按常用度排序（苏州在宿州前、福州在抚州前）。/ The nine-key and QWERTY lexicons gain 293 Chinese prefecture-level cities, so `hangzhou` yields 「杭州」 directly instead of requiring two single-character picks; homophones are ordered by common usage (苏州 before 宿州, 福州 before 抚州).
- 定位改用内置城市坐标表：297 个地级及以上城市精确命中即取坐标，不再每次跨境做地理编码，实测从 8521ms 降到 6632ms；表外地名仍回落到联网检索。/ Locate now reads a built-in table of 297 prefecture-level city coordinates, so an exact hit skips the cross-border geocoding call and the measured action drops from 8521 ms to 6632 ms; off-table names still fall back to the geocoder.
- 跟进上游 1.0.7–1.0.9 的八项功能：前光上限降至 10%、实体键关灯状态修复、页脚进度条显隐、书摘随已显示进度更新、关闭书内样式统一排版、解锁与退出阅读残影优化、WiFi 传书队列与上传速率、阅读四向重力旋转与方向锁定。/ Port eight upstream 1.0.7–1.0.9 features: a 10% frontlight ceiling, the bound-key light-off fix, footer progress visibility, excerpts that follow the displayed page, an in-book style override, lighter unlock/exit ghosting, a transfer queue with upload rate, and four-way reader rotation with direction lock.
- 打开目录时自动翻到「当前页所在条目」那一页；分节目录里同一章占多条时按本章已读页数插值，不再一律停在章首。/ The directory now opens on the page holding the entry for the current page, interpolating by pages read inside a chapter instead of always stopping at the chapter head.
- 书摘优先从当前页取句，取不到可用整句才回落到最近几页；此前窗口可达四五页，摘出的句子常和眼前内容无关。/ Excerpts are taken from the current page first and only fall back to the last few pages when that page has no usable sentence; the old window spanned four or five pages, so quotes often read unrelated to what was on screen.

## v1.0.6 · 2026-11-06

- 重构书架分类功能。
- 支持保存多个 WiFi。
- 支持 MOBI 格式。
- 提高底部三键灵敏度。

## v1.0.5 · 本地测试 / Local testing · 2026-11-06

- FT6336U 检测阈值采用模组复位原值的75%，提升弱触摸响应；支持恢复100%原阈值，初始化与唤醒均读回校验、失败恢复。此为通用阈值，不是三键专用参数，不同厚膜、屏幕误触与充电噪声仍需对应样机验证。/ Use 75% of the FT6336U module reset threshold for weaker touches, with 100% available to preserve the original and verified readback/rollback at init/wake. This is a general threshold, not key-specific; different protectors, screen false touches and charger noise still need hardware checks.
- 修复刷新等阻塞期间盖板左、中、右三键积压回放漏按；保留原按键映射，中键短按/长按按采样时间区分，切页后的新返回请求不再被清除。宿主回归覆盖实时与回放交错。/ Fix lost left/middle/right panel presses buffered during refresh and other blocking work; preserve mappings, distinguish middle taps/holds by sample time and retain fresh Back requests after navigation. Host regressions cover mixed live/replayed input.
- WiFi 最多保存八个网络并兼容旧配置，附近列表标记已保存，新增已保存列表及连接、改密码、逐个确认遗忘；存满不覆盖。修复翻页箭头缺字，移除底部两行说明，改为每页六行。/ Save eight WiFi profiles with legacy compatibility and saved markers; manage connections, passwords and confirmed individual removal without eviction. Use glyph-independent paging icons and six rows instead of footer hints.
- 新增无 DRM 传统 MOBI 原生阅读，支持未压缩/PalmDOC、UTF-8/Windows-1252、目录、续读、书签、JPEG/PNG 封面与插图，接入书架/文件管理/网页传书。混合书固定读取 MOBI6；暂不支持 HUFF/CDIC、纯 KF8、DRM 和书内字体。保持 TXT/EPUB 兼容，实机待验收。/ Add native DRM-free traditional MOBI with raw/PalmDOC, UTF-8/Windows-1252, navigation/resume/bookmarks, JPEG/PNG covers/images and shelf/files/web transfer integration. Hybrids always use MOBI6; HUFF/CDIC, pure KF8, DRM and embedded fonts are unsupported. Preserve TXT/EPUB compatibility; device acceptance pending.
- 移除书架题头下重复的书籍数量，搜索、选择、管理、导入改为同排等距，点击区域同步调整；底部分页数量与多选布局保留。/ Remove the duplicate shelf header count and evenly space search, select, manage and import in one row with matching hit targets; retain the pager count and selection layout.

## v1.0.5 · 本地测试 / Local testing · 2026-11-05

- 调整分类选书与书架多选间距，为筛选和批量操作增加独立按钮；底部固定已选核对与提交，书架多选暂时隐藏导航，类内移出收进更多菜单。修复未选书时「添加」被灰块覆盖，禁用态保留白底灰字；普通书架布局不变，实屏待验收。/ Separate category filters and batch actions into spaced controls, pin review/submit actions, hide navigation during shelf selection and move scoped unclassification into More. Fix the disabled Add control's gray block with gray ink on white; normal browsing is unchanged and panel acceptance is pending.
- 书架改用独立分类，不搬动原文件；新建后立即选书，支持跨页/搜索/筛选勾选、已选核对、类内添加、批量归类/移出、改名/顺序/删除及最近一次撤销。旧目录只迁移一次，双副本保存按TF卡隔离；设置备份暂未纳入分类资料，真机验收待完成。/ Independent categories leave files untouched; create/pick, retained selections/review, add/classify/unclassify, rename/order/remove and session undo. Migrate folders once and persist checked copies per card; settings backup and device acceptance remain pending.
- 修复分类命名未走编辑刷新出口的问题，恢复按键反馈、输入与键盘切换；用户已确认旧版键盘实机恢复，新版分类仍待实屏验收。/ Restore category key feedback/input/layout changes through the editor presentation path; the prior keyboard fix was user-confirmed, while logical categories await device acceptance.

## v1.0.5 · 2026-11-05

- 重新设计阅读手账像素画风格。
- 优化天气接口逻辑，不再强制校验设备时间。
- 移除文件管理内文件夹最大显示96个文件的限制。
- 优化中段电量显示。
- 文件页面 UI 改版。

## v1.0.4 · 本地测试 / Local testing · 2026-11-05

- 文件页移除大标题和外框，存储信息与三种传输入口改为简洁排版；根目录每页六行，子目录九行，长文件名保留扩展名并显示省略号，点击区域同步调整。/ Simplify Files with unframed storage and three icon-labelled transfer routes, six root rows and nine directory rows; ellipsize long names while retaining extensions and align touch targets with the new layout.
- 移除文件管理单目录最多96项的固定限制，列表按需使用外部内存；内存不足时明确提示，不再静默显示不完整目录。/ Remove the file manager's fixed 96-entry directory limit with an on-demand PSRAM list; report insufficient memory instead of silently showing an incomplete directory.
- 页眉页脚设置收紧标题下方留白，首屏同时显示六个内容槽位；高度调节与恢复默认仍通过滑动访问。/ Reduce whitespace below the header/footer settings title, showing all six content slots together; height controls and reset remain accessible by scrolling.
- 优化电量显示：依据连续采样修正中段相对曲线，修复插拔时负载补偿反向，普通区间每分钟最多变化 1%；完善重启保存、判满采样连续性和低电量尾数处理。低段仍待整周期验证，不改变硬件充电及欠压保护。/ Improve battery display with sampled midrange estimates, signed load compensation, normal-range 1% per minute steps, restart checkpoints, continuous full confirmation and empty-level settling. Full-cycle low-range validation remains pending; hardware charging and undervoltage protection are unchanged.
- 默认头像改为压缩后的 kimiko logo，默认锁屏与开机准备画面共用同一张 684×1216 logo 图片，移除旧文字锁屏及独立 init-loading 资源；保留自定义头像和壁纸。/ Use the compressed kimiko logo for default avatars and one shared 684x1216 logo image for fallback lock and startup screens, removing the old text art and separate init-loading asset while preserving custom avatars and wallpapers.
- 本地快速刷机脚本默认及最大时限由 110 秒放宽到 180 秒，保留 10 秒失败清理、活动应用分区限定、完整校验与启动确认。/ Extend the local fast-flash helper's default/maximum deadline from 110 to 180 seconds, preserving ten-second cleanup, active-app-only writes, complete verification and boot confirmation.
- 修复设备时间偏差导致正常天气被拒收：预报校验、跨日显示和自动刷新使用可信 HTTPS 响应的服务器时间及单调计时，保留原始有效期和异常检查，不修改系统时钟或 RTC；宿主回归通过，未刷机验收。/ Fix normal weather rejection caused by device-clock skew using trusted HTTPS server time plus monotonic elapsed time for validation, day selection and refresh, preserving original expiry and anomaly checks without setting the system clock or RTC; host regressions passed, with device acceptance pending.

## v1.0.4 · 本地测试 / Local testing · 2026-11-04

- 固件与传书网页品牌统一为 `kimiko`，默认设备名为 `kimiko`、热点名为 `kimiko-XXXX`、口令为 `kimiko1234`；启动及恢复配置时迁移历史默认设备名，保留自定义名称与旧备份兼容。/ Rebrand the firmware and transfer page as `kimiko`, with device default `kimiko`, hotspot `kimiko-XXXX` and password `kimiko1234`; migrate historical default names on boot/restore while preserving custom names and backup compatibility.
- 阅读手账替换全部旧像素插画，改为沿途有书、街角书店、纸上花园、月面观测站、唱片聆听角和灯塔书屋；保持六图轮换、同日选择保存及原有布局，空记录也使用当前插画。/ Replace all old journal illustrations with Books Along The Way, Corner Bookshop, Paper Garden, Moon Observatory, Vinyl Listening Corner and Lighthouse Library, preserving six-scene rotation, saved choices and layout; empty journals use the current scene too.

## v1.0.4 · 2026-11-04

- 天气组件补充雷阵雨、雾霾背景。
- 新增阅读手帐组件。
- 修复部分 EPUB 书籍无法打开的问题。
- 优化 EPUB 书籍，支持 WebP 图片。

## v1.0.3 · 本地测试 / Local testing · 2026-11-04

- 修复含冒号等特殊文件名的部分 EPUB 无法打开；新增静态 WebP 封面/插图解码，并将正文 body 的单图背景保留为插图，重建旧自动封面缓存。原书无需转换，宿主回归通过，两个问题样本已刷入设备并由用户确认恢复。/ Fix opening EPUBs with literal-colon archive names; add static WebP covers/illustrations and body single-background illustrations, rebuilding old fallback-cover caches. No book conversion; host checks passed and the user confirmed recovery of both reported samples after flashing.
- 新增可选半屏「阅读手账」：首页显示当天书目、时长、章节和翻页次数，点击进入当月日历查看逐日明细；六幅像素插画每天轮换，也可点图手动切换，同日选择保存。明细保存在本机，不纳入现有设置备份，实机效果待验收。/ Add an optional half-screen Reading journal with today's books/time/chapters/turns, a current-month daily calendar and six persistent daily/manual pixel scenes. Records stay local and outside existing settings backups; device acceptance remains pending.
- 首页续读按下反馈改为封面外缘黑色描边，不再移动灰阶封面，避免长按进入组件页时遗留旧封面底边；不增加整页强刷，实屏改善待验证。/ Keep Home cover pixels fixed and show a black margin outline on press to avoid an old bottom edge after holding into Components; no extra full-page refresh, with panel improvement awaiting verification.
- 天气组件补充沙尘、雨雪混合与阴天三种背景，按官方天气代码区分；雾和霾共用雾景，移除独立霾图。热、冷和未知状态仍保持中性。/ Add Dust, mixed rain/snow and Overcast backgrounds mapped by official condition codes; Fog and Haze share the fog image, with no separate Haze asset. Hot, Cold and Unknown remain neutral.
- 雾景改用林梢与雾带遮挡；雷阵雨使用独立云雨构图，闪电主干从云底贯穿组件下沿，增加自然分叉、不规则转折、宽窄变化和亮芯，区别普通下雨。实屏效果待验收。/ Use mist-obscured trees for Fog and a separate thunderstorm composition with a cloud-to-bottom lightning trunk, natural side channels, irregular turns, varied width and bright cores; panel appearance awaits acceptance.

## v1.0.3 · 2026-11-03

- WiFi/热点传书页面重构，更加美观便捷。
  Redesign the WiFi/hotspot transfer page for a cleaner, more convenient file workspace.
- 页眉页脚增加自定义高度调节。
  Add independent custom height controls for the reader header and footer.
- 重构组件库，移除时钟、阅读记录等组件，新增天气组件。
  Rework the component library, remove the old clock and reading-record widgets, and add weather.

## v1.0.2 · 本地测试 / Local testing · 2026-11-03

- 天气响应等待放宽至5秒并保留分段取消；城市搜索按20条批次暂存，界面每页8条本地切换，仅在缺少结果批次时补取，避免每次翻页重连查询。/ Allow a cancellable five-second weather response window and cache 20-result city batches for local eight-row paging, fetching only missing batches.
- 天气城市结果改为每页8条、36px城市名与24px行政区分行显示，整行点击及底部翻页利用键盘收起后的空间；展开键盘时隐藏结果与翻页，避免误选。/ Show eight weather-city results per page with 36px names, 24px administrative sublines, full-row targets and bottom paging; hide result actions while typing.
- 共用键盘与首个候选改为白底黑字，候选放大至32px；修复城市页把候选清理降为差分刷新的问题，候选变化3次、停手400ms后仅做词栏全像素灰阶整理，不打断连续输入或增加整屏清理。实屏残影改善待验证。/ Use white keys/candidates with 32px candidate text, preserve the city page's full-pixel settle and clean only the word strip after three changes plus a 400ms pause; panel ghosting improvement remains unverified.
- 移除天气请求的临时阶段与耗时日志，不改变请求超时、取消或资源交还。/ Remove temporary weather request phase/timing logs without changing timeouts, cancellation or resource cleanup.

## v1.0.2 · 本地测试 / Local testing · 2026-11-02

- 修正天气正式接口为 `https://kimiko.mantake.shop`，同步 OTA 平台地址说明；OTA 主源与下载校验保持使用该平台。/ Correct the production weather origin to `https://kimiko.mantake.shop` and align OTA documentation with the existing platform feed and download validation.
- 天气城市搜索改用平台官方 CSV 城市字典，支持中文、英文、拼音及首字母，完整结果分页并展示市、省归属；预报统一按官方地点 ID 共享六小时缓存，自动定位直接返回中文，不再逐次查询和风修正名称。本地开发旧选城格式不迁移。/ Use the persistent official city dictionary with bilingual/Pinyin/initial search, full pagination and administrative labels; share six-hour forecasts by official ID and return Chinese IP results directly, without repeated Geo relabeling or development-format migration.
- 修正官方字母数字地点 ID 的校验，避免部分台湾地点能搜到却无法选择或保存。/ Accept official alphanumeric location IDs so listed Taiwan locations can be selected and persisted.

- 新增首页组件库与可选天气卡片：城市自动识别/手动搜索、今日明日本地切换、最低最高温度、白天湿度及图标日出日落；通过平台代理访问和风，缓存过期不展示旧数值。平台默认停用，正式启用仍须完成凭据、许可与设备验证。/ Add opt-in proxy weather with local day switching and bounded cache; activation still requires credentials, licensing and device validation.
- 天气城市名称优先使用中文，补齐默认字体的「庄」字；首页入口改为长按天气卡片，设置页阅读背景下方增加组件库入口，组件库底栏只保留首页按钮。/ Use Chinese weather city names and add the missing default-font city glyph; enter the library by holding the Home card or from Settings below Reading background, and keep only Home in the library footer.
- 所在城市页合并自动定位入口，键盘改为点输入框展开、点空白收起并保留输入，底栏移除两侧返回图标及热区。/ Unify automatic location in the city picker, show its keyboard only on field taps and preserve input when dismissed, and remove both side footer buttons and hit areas.
- 天气组件增加默认开启、可保存的六小时自动刷新开关，保留手动刷新并在到期/跨日缺预报时补取，不主动唤醒待机；最低最高温度改用斜杠分隔。/ Add a persisted, on-by-default six-hour weather refresh toggle with manual updates and expiry/midnight recovery without standby wake; separate minimum/maximum temperatures with a slash.
- 首页改为上下等高组件区，可选择最近阅读、天气或留白，同类组件自动交换而不重复；天气去除边框、圆角及导航前白条。新版设置备份支持组件布局，恢复旧备份保留当前布局。/ Add two equal Home regions with unique reading/weather widgets and repeatable empty choices; remove weather frames, rounded corners and the footer gap. New settings backups include layout; old backups preserve current choices.
- 天气刷新改为靠右的单图标，移除「刷新」文字、天气底部及最近阅读右上角的设置图标；固定导航区缩到实际导航顶部，释放上方24px为内容，修复切页和局部刷新残留。/ Use a right-aligned refresh icon without a label, remove weather/reading settings icons, and shrink fixed navigation to its actual top so all content updates include the formerly protected 24px.

- 移除首页最近阅读卡片下方的书夹、时钟与样式选择、月阅读记录及闭环目标，顶部关联圆环一并撤下；下半区留白，保留续读、头像、状态栏时钟与底部导航，不清除阅读进度或票根统计。
  Remove the old folder/clock widgets, style picker, monthly reading view and daily goal with its header ring. Leave the lower Home area blank while retaining resume, avatar, status clock and navigation; preserve reading progress and ticket statistics.
- 传书网页文件列表增加左右留白，避免选中与悬停底色紧贴复选框和更多按钮；表头同步对齐。
  Add horizontal padding to transfer file rows so selected and hovered backgrounds leave space around checkboxes and action buttons; align the header to match.
- WiFi 与热点传书网页改为纯白文件工作台：桌面并排显示目录与上传记录，壁纸和签名单独切换，手机首屏可添加文件；保留分页和覆盖确认，增加本页搜索/排序、批量删除及失败重试。尚未发布，真实设备传输待验收。
  Redesign WiFi/AP transfer as a white file workspace with side-by-side desktop browsing and transfer history, separate wallpaper/signature views and first-screen mobile upload. Retain paging and overwrite confirmation; add current-page search/sort, bulk deletion and failed-upload retry. Unreleased; real-device transfer awaits acceptance.
- 阅读设置的页眉页脚增加独立高度调节，正文与刷新区域同步调整，保持阅读位置；默认布局与全屏排版不变，支持保存、备份和恢复默认。
  Add independent header/footer height controls with anchored body reflow and matching refresh bounds; retain default/full-screen geometry, persistence, backups and reset.
- 为内置字体补齐「眉」「锋」的原生轮廓，避免设置文字放大低分辨率补充位图；实屏效果待验证。
  Add native outlines for the two reader-label glyphs instead of enlarging low-resolution supplemental bitmaps; on-device appearance remains unverified.

## v1.0.2 · 2026-11-02

- 优化浅睡时钟兼容与待机电量读取，减少重复总线访问；实际待机电流与节电比例尚未测量。
  Improve light-sleep clock compatibility and battery sampling, reducing duplicate bus access; standby current and savings have not been measured.
- 增加默认关闭的晃动翻页，可在翻页模式中开启，并过滤触摸、回弹与缓慢倾斜。
  Add off-by-default shake page turning in turn-mode settings, filtering touch, rebounds and slow tilts.
- 增加页眉页脚左、中、右三格内容自定义，并修复空中格时两侧文字重叠。
  Add customizable left, center and right header/footer slots and fix side-text overlap with an empty center.
- 调整亚克力书架挡板高度及设置界面，隐藏系统升级中的 TF 卡入口，保留底层升级能力。
  Adjust acrylic shelf guard height and settings UI; hide the TF upgrade entry while retaining its underlying support.
- 待机方式增加「浅睡后关机」，支持 10/30/60 分钟；每轮锁屏重新计时，提前唤醒取消。实际定时与断电行为尚待真机验收。
  Add light-sleep-then-off after 10/30/60 minutes, restarting on each lock and cancelling on early wake; physical timing and shutdown await device acceptance.
- 增加默认开启的自动 OTA 检查开关，每 12 小时在可用空闲窗口检查，发现新版提示确认，不自动下载安装或主动唤醒待机设备。
  Add on-by-default automatic OTA checks every twelve hours in an available idle window, prompting for new versions without automatic download, installation or standby wake.

## v1.0.1 · 本地测试 / Local testing · 2026-11-02

- 系统升级隐藏 TF 卡升级入口，新增默认开启的自动 OTA 检查：固定每 12 小时在可用空闲窗口检查，发现新版本提示确认，不自动下载安装；待机不主动唤醒。
  Hide the TF upgrade entry and add on-by-default automatic OTA checks every twelve hours in an available idle window, offering new versions without automatic download, installation or wake.

- 浅睡成功 GPIO 唤醒后保全实际读到的电源键按下，避免复查期间松手丢失；不将来源不明的共享中断当成电源键，保留 2 秒定时兜底。首次读取前已经松手的判源限制仍保留。
  Preserve an observed power-key press after successful GPIO wake so release during rechecking cannot lose it; do not treat unknown shared interrupts as POWER, retaining the two-second timer fallback. A press released before the first read may still be unidentifiable.
- 增加默认关闭的浅睡诊断：会话内仅 RAM 计数，退出后保存一份汇总供重启读回，不新增唤醒或逐切片落盘。统计不是电流测量，尚无实测省电结论。
  Add off-by-default light-sleep diagnostics: RAM-only counters during waits and one saved summary after exit for boot replay, without extra wakeups or per-slice writes. Timings are not current measurements; no measured power-saving result is available.
- 浅睡按键检查短暂复用不足 10 ms 的完整有效电量样本，减少紧邻检查的重复总线访问；按键每次读取，到期充电维护与采样失败仍重读，活动页完整轮询不变。
  Sleep key checks briefly reuse complete valid battery samples younger than 10 ms to avoid adjacent duplicate bus accesses; keys are always read, due charger maintenance and sampling failures force refresh, and active-page full polls are unchanged.
- 设置「待机方式」新增「浅睡后关机」，选中后在卡片内选择 10/30/60 分钟，保留原有立即关机与仅浅睡。每次锁屏浅睡重新计时，提前按键或拿起唤醒取消本轮计时；密码锁屏同样生效，关机失败仍保持锁屏。设置随配置备份保存，实际定时与断电行为待真机验证。
  Standby settings add light-sleep-then-off with inline 10/30/60-minute buttons when selected, retaining immediate off and light-only. Each locked sleep starts a fresh countdown; an early key/pickup wake cancels it. PIN locks follow the same policy, and failed shutdowns stay locked. The choice is included in settings backups; physical timing and power-off await device validation.

## v1.0.1 · 本地测试 / Local testing · 2026-11-01

- 亚克力书架挡板高度减半，保留底边位置以露出更多封面；设置预览同步调整。
  Halve acrylic shelf guard height while retaining its bottom edge to reveal more cover art; update the settings preview accordingly.
- 修复页眉页脚中间为空或隐藏时两侧文字重叠、列表视口外选项仍可点击；恢复默认布局保存失败时显示提示。
  Fix overlapping side slots when the center is empty or hidden and taps on off-viewport options; report failures when saving the default layout.
- 晃动翻页持续屏蔽单指与多指接触，抬手后保留静默期；增加按采样间隔归一的横向变化速率门槛，过滤缓慢倾斜，并在传感器休眠失败后退避重试。新判定仍需真机复测。
  Shake turns stay suppressed during single/multiple contacts and briefly after release; add a time-normalized horizontal slew gate against slow tilts and back off/retry failed sensor sleep. The revised gate still needs device validation.

## v1.0.1 · 本地测试 / Local testing · 2026-10-31

- 阅读新增「页眉页脚」自定义：页眉页脚各分左/中/右三格，在阅读设置内自选显示内容（书名、章节名、页码、进度、时间、电量、日期星期、电量图标或不显示）。默认页眉变为书名/时间/电量图标，页脚保持章节名与页码·全书进度；恢复默认可一键还原。设置随备份文件保存。真机显示效果待验证。
  Reading adds header/footer customization: each bar splits into left/center/right slots picked in the reading sheet (book title, chapter, page, progress, clock, battery, date, battery icon or blank). The default header becomes title/clock/battery icon and the footer keeps chapter plus page·progress; a reset row restores defaults. Settings ride the backup file. On-device appearance awaits verification.

- 阅读新增「晃动翻页」：翻页设置里默认关闭，开启后正文横向晃动翻页，左晃上一页、右晃下一页；触摸与回弹不触发，离开正文或锁屏时加速度计自动休眠。晃动方向以真机校准为准。
  Reading gains shake page turning: off by default in the turn settings; when enabled, a horizontal shake in the reading body turns one page, left back and right forward. Touch and rebounds do not trigger it, and the accelerometer sleeps automatically outside the body or on lock. Shake direction remains subject to on-device calibration.

- 电量日志增加版本与构建标识、实际采样时间、有效性、锁屏/唤醒及电源事件，分别记录采样覆盖与容量估算；加入文件轮转、写入失败计数和 TF 交接保护。电量曲线保持不变。
  Battery logs add build identity, actual sample times, validity, lock/wake and power events, separating sample coverage from capacity estimates, with rotation, write-failure counters and TF handoff protection. The battery curve is unchanged.

- 浅睡引入原厂 S31 MPLL 唤醒校准及 PSRAM 时钟交接兼容处理；实际待机电流尚未测量。
  Light sleep adopts the vendor S31 MPLL wake-calibration and PSRAM clock-handoff workaround; standby current has not been measured.

- 浅睡时关闭 P14 蓝牙／功放供电，唤醒后恢复；触摸复位脚 P12 保持高电平。
  Light sleep cuts the P14 Bluetooth/amplifier supply and restores it on wake, keeping touch reset P12 high.

## v1.0.1 · 2026-10-30

- 本次交付统一使用1.0.1，包含下列高压关断、阅读刷新及本地测试中的 TXT 章节目录、字体列表、电源键、蓝牙、电量和界面调整。开书、连续翻页和保存阅读设置已通过本轮真机复测，实际高压归零时刻仍未测量。
  This delivery consistently uses 1.0.1, including the power-off and reader-refresh changes below and the TXT chapters, font list, power key, Bluetooth, battery and UI changes recorded under local testing. Book opening, consecutive page turns and reader-setting saves passed this round of device checks; the physical time at which the high-voltage rails reach zero has not been measured.

- 本地调整（未发布）：高压改为真实扫描前上电、完整显示组结束即关电，组内仍由独立任务执行扫描后100ms空闲关断；关电先撤MODE/XOE并等待至少12µs，再关闭供电。设置保存与OTA写入前确认关电并阻止扫描，保留通信失败重试及马达共享扩展口并发保护。实际电压时序与光学效果需真机验证。
  Local change (unpublished): power on immediately before physical scans and shut down when a complete display group ends, retaining independent 100 ms idle shutdown within groups. Lower MODE/XOE and wait at least 12 µs before disabling power. Settings and OTA writes confirm shutdown and exclude scanning, retaining communication retries and concurrent motor/expander protection. Actual voltage timing and optical behavior require device verification.

- 本地调整（未发布）：阅读页眉恢复默认裁剪 GL16，普通页脚恢复短 DU，换章和插图页脚保留默认 GL16 清理，全屏进度恢复默认 GL16；保留正文优先与状态随翻页更新。实际速度、墨度和残影待实屏验证。
  Local change (unpublished): restore trimmed default GL16 for reader headers and full-screen progress, short DU for ordinary footers, and default GL16 cleanup for chapter/illustration footers. Keep body-first presentation and status updates with turns. Actual latency, ink density and ghosting await panel verification.

## v1.0.2 · 本地测试 / Local testing · 2026-10-30

- 界面底色与控件统一白、黑及固定灰，小字改黑，卡片和导航改白底；保留书架亚克力挡板、封面、图像及抗锯齿。快捷栏使用不透明白底，前光滑杆改为黑白差分刷新。
  UI surfaces and controls use white, black and one fixed gray, with black small text and white cards/navigation. Retain acrylic shelf guards, covers, artwork and antialiasing. The quick layer is opaque white and frontlight sliders use binary differential refreshes.

- WiFi传书连接框与传输状态框改为黑色边框；底部导航只保留图标，高度104像素、图标居中，分隔线加粗并使用0x90灰，控件固定灰加深至0x90。前光滑杆复用完整 DU 并扩边擦除；快捷栏收起恢复整屏归一化，按当前页面保护阅读背景灰阶，退出阅读回主页面（含书架）恢复整屏GC16清理文字残影。实际残影效果待实屏验证。
  WiFi transfer connection/status cards use black frames; the footer uses centered icons in 104 pixels with a thicker 0x90 gray divider and darker 0x90 control gray. Frontlight sliders reuse complete DU with padded dirty masks; closing the quick layer restores whole-screen normalization with reading-background gray protection selected by the current page, and reader-to-main returns (including the shelf) restore whole-screen GC16 to clear text residue. Optical residue awaits panel acceptance.

- 阅读页时间、电量和充电状态改为随翻页更新，静置不单独刷新顶部；正文优先输出，随后用阅读GL16更新页脚和顶部，保留水波纹及手动/周期全刷。累计变淡改善效果待实屏验证。
  Reader time, battery and charging status update with turns without idle top-band refreshes; the body presents first, followed by reader GL16 footer and status output, retaining ripple and manual/periodic full refresh. Fading improvement awaits panel verification.

## v1.0.2 · 本地测试 / Local testing · 2026-10-29

- 电源键增加独立快速采样，保留阅读排版与刷屏期间的短按；锁屏浅睡前同步暂停，解锁后的原有保护时间保持不变。
  Independently sample the power key to retain short presses during reader layout and refresh; synchronously pause before locked sleep and retain the existing post-unlock guard.

- 主页面、顶部快捷栏和阅读页可见的状态栏及页眉，在电量、充电状态变化时自动局部更新；开机首帧先尝试采样电量，避免必须切页才能恢复显示。
  Main pages, the top quick layer and visible reader status bars and headers locally update battery and charging changes; sample before the first boot frame so recovery no longer depends on switching pages.

- TXT 目录支持较长的篇卷与章节复合标题、全角缩进和连续重复题头合并；旧目录缓存自动重建，无有效章节时继续按 Part 分段。
  TXT indexes recognize longer volume/chapter headings and full-width indentation, merge consecutive repeated headings, and rebuild old caches; texts without usable headings retain Part segmentation.

- 阅读页和设置页的字体列表按实际数量扩容，取消原先最多收录 24 款字体的限制，继续分页显示。
  Reader and system font lists grow with the directory contents, removing the previous 24-font cap while retaining pagination.

- 修复部分蓝牙翻页器锁屏重连后，已学习的按键被误判为静止帧而失效的问题。
  Fix learned keys on some Bluetooth page turners being mistaken for idle reports after lock-screen reconnection.

- 卡失效后自动重挂前，确认封面和头像后台任务已交还文件操作；仍忙时延后重试，避免卸载正在使用的旧文件系统。
  Automatic remount after media invalidation checks that cover and avatar jobs have returned file ownership, deferring while busy to avoid unmounting a filesystem still in use.

- 屏幕电源扩展器初始化遇到暂时通信错误时，最多恢复总线并重试两次；检查最终输出写入结果，避免一次失败后屏幕一直无法更新。
  Screen power-expander initialization recovers the bus and retries up to twice after transient communication failures, checking the final output write so one failure does not permanently disable presentation.

- 按原厂驱动对齐屏幕上电检查、VCOM 写入顺序和完整掉电重试；四次尝试仍失败时重启，供电就绪后才允许扫描。
  Align panel checks, VCOM write order and full power-cycle retries with the vendor driver; restart after four failed attempts and enable scanning only on ready rails.

- 修复扩展器寄存器快照与双口清中断，将拾起唤醒改为原厂共线低有效配置；共享 I2C 增加完整事务保护与读取回退。
  Fix expander snapshots and dual-port IRQ clearing, use vendor active-low shared pickup wake, and protect complete I2C transactions with read fallback.

- 补齐充电终止、补充充电和欠压维护；保留电量显示算法，修复触摸异常帧、盖板键范围和 RTC 的启动及 UTC 读写。
  Add termination, recharge and undervoltage maintenance while retaining the battery display algorithm; fix invalid touch frames, cover-key bounds and RTC startup/UTC handling.

## v1.0.0 · 本地测试 / Local testing · 2026-10-27

- 联网升级改为使用 Kimiko 平台的 E4P 固件清单和下载地址，继续验证设备型号、分区、镜像大小与 SHA-256。
  Online updates use the Kimiko platform E4P feed and download origin, retaining board, layout, size and SHA-256 checks.

- 恢复设置页「系统升级」入口，并将固件内部版本号统一为 `1.0.0`；本地 OTA 验证可通过独立构建配置连接测试平台。
  Restore the system-upgrade entry and use `1.0.0` as the embedded version; isolated build configuration supports local OTA testing.

- 开机图与默认锁屏图改为无损压缩存储，逐行解压绘制，保留原有像素及旋转显示，减少 app 占用；本地测试沿用当前版本，未发布。
  Splash and fallback lock art use lossless embedded compression and row-by-row decoding, preserving pixels and rotation while reducing app usage. Local testing retains the current version; not published.

## v1.0.0 / 2026-10-27

正式发布版。品牌定名 kiikoread；汇总 rc98 以来全部本地改动，含电量显示稳定化与中段 OCV 锚点回充校准、WiFi 传输对齐原厂提速、快捷栏前光重排与滑杆专用波形、触摸震动、阅读背景与插图边距、EPUB 脚注展开、彻底关机恢复等；逐条细节见下方 rc98–rc101 本地条目。
Official release. Branded kiikoread; consolidates everything since rc98, including the stabilized battery display with mid-curve OCV anchors recalibrated by refill accounting, vendor-aligned WiFi transfer speedup, the quick-layer frontlight redesign with its drag waveform, touch vibration, reading backgrounds and illustration margins, EPUB footnote sheets, and the restored full power-off. Per-item details remain under the rc98–rc101 local entries below.

- 电量校准：用卡上日志的回充对账修正 OCV 曲线中段锚点。新日志含一次满电深放 8.4 小时到 4075 mV 再回满的完整循环，从 4092 mV 静置回满只净充 346 mAh、另一次夜间放电回充 ~93 mAh，两条回充腿与放电库仑积分互相一致（容量按规格 2050 mAh），据此把 4300/4200/4100 mV 锚点从 900/800/680‰ 修到 955/875/790‰——原中段为无数据推测值，偏低 6–11%，深放电底点的显示从 68% 修到 77%（BQ 内部库仑计当时读 85%）。内阻复核 14 次切换中位 206 mΩ、1 A 大步 175–192 mΩ，维持 180 mΩ。4.09 V 以下仍无放电数据，低段锚点不动、不外推，等一次放电到关机。对拍测试新增回充锚点断言；`test_pmu_battery.py` 补上坏 SDK 的编译回退并剥掉本机会挂的 ASan。
  Battery calibration: the mid-curve OCV anchors are corrected by refill accounting from the card log. The new log contains a full cycle—full pack discharged 8.4 h to 4075 mV then recharged—so refilling from 4092 mV resting took only 346 mAh net, a second overnight leg took ~93 mAh, and both refill legs agree with the discharge coulomb integral (capacity per spec, 2050 mAh). The 4300/4200/4100 mV anchors move from 900/800/680‰ to 955/875/790‰: the old mid anchors were guesses and read 6–11% low, so the deep-discharge floor now shows 77% instead of 68% (the BQ internal gauge read 85% there). Pack resistance re-checked at 14 switches (median 206 mΩ, the three 1 A steps 175–192 mΩ); 180 mΩ stays. Below 4.09 V there is still no discharge data, so the low anchors are untouched and not extrapolated—awaiting one full discharge to shutdown. The replay test gains refill-anchor assertions, and `test_pmu_battery.py` gets the broken-SDK compile fallback with the hanging ASan stripped.

## 0.3.3-rc99 / 2026-10-23

正式发布版，汇总 rc98 本地构建的全部改动；逐条细节见下方 rc98 本地条目。
Official release consolidating the rc98 local builds; per-item details remain under the rc98 local entries below.

- 前光：下拉快捷栏改为圆钮开关加整行大滑杆，亮度上限降到原厂满输出的 30%；按键控制新增「前光开关」动作。浅睡灭灯、唤醒恢复。
  Frontlight: the quick layer uses a circular switch with full-row sliders, the brightness ceiling drops to 30% of factory output, and key controls gain a frontlight toggle; light sleep blanks it and wake restores it.

- 按键：新增「上键单击」映射和「音量键翻页」开关；按键控制示意图改为与机身实体顺序一致，动作列表扩到八行并修复下方选项点错。
  Keys: the top key gains a single-press mapping and the side volume keys can turn pages; the key sheet follows the physical order, lists eight actions and no longer fires the wrong row.

- 翻页与刷新：补回原厂 18 次推黑，修复翻页后正文偏灰；面板高压轨改为每次刷新后只保留 100 ms；修复「仅浅睡」唤醒后主页刷新降速。
  Turns and refresh: the vendor's 18 black pushes are restored so body text stops looking gray, the panel HV rails now drop 100 ms after each refresh, and light-sleep wakes no longer slow the home page.

- 设置：配置备份升到 v27，保存书内字体、竖排正文与三种关机方式；无操作自动锁屏默认 5 分钟，关机默认「仅浅睡」；WiFi 对时更稳定。
  Settings: backups move to v27 covering embedded book fonts, vertical text and all three shutdown styles; idle lock defaults to five minutes and shutdown to light-only; WiFi time sync is more reliable.

- 设置页不提供「升级和恢复 → 系统升级」入口：OTA 尚未就绪，联网与 TF 卡升级页面代码保留但不可达。
  The settings page offers no System Upgrade row until OTA is ready; the online and TF-card update pages keep their code but stay unreachable.

## 0.3.3-rc101 本地 / 2026-10-25

- 锁屏密码：修复设密码后输完解锁、页面乱跳的问题——输密码的四次点击此前会留在触摸采样回放队列里（主循环阻塞在密码层，队列无人消费），解锁后被当作新手势整批回放到已解锁页面；现在每次锁屏返回主循环即作废全部积压触摸帧并复位边沿状态，普通浅睡唤醒路径同样受益。
  Lock PIN: fixed the unlocked page jumping around after typing the password—the four PIN taps used to survive in the touch sampler replay queue (the main loop stays blocked in the PIN layer, so nobody drains it) and were replayed onto the unlocked page as fresh gestures. Every lock return now discards the whole backlog and resets edge state; ordinary light-sleep wakes benefit the same way.

- 按键：「上键单击」绑定「开关前光」后，退出阅读回到主页/书架也能正常开关前光；其余上键动作仍只在阅读正文生效，阅读内行为不变。
  Keys: with the top key bound to the frontlight toggle, leaving the reader for the home/shelf pages no longer kills the binding; other top-key actions stay reader-body-only and in-reader behavior is unchanged.

- 快捷栏：前光关闭时打开的快捷栏，继续下拉也会展开亮度、色温胶囊滑杆——同一次下拉需先停顿约 0.3 秒再往下拉过一段，或在已开的层上再往下拉一次；一次连续快滑只开层、不会连带展开。展开只属本次开层，收起再打开仍只有四个圆钮，灯灭时拖动滑杆只记住档位不点亮，点太阳开灯或熄灯同样收起。
  Quick layer: with the frontlight off, continuing the pull also expands the brightness and colour-temperature capsules—either the same pull paused for ~0.3 s and then pulled further down, or a fresh downward drag on the open layer; one continuous fast swipe only opens the bar without chaining the expansion. The expansion lives only for that opening: closing and reopening returns to the four circles, dragging while the light is off only queues the levels without lighting, and the sun tap still expands or collapses.

- 电量校准日志落 TF 卡 + 涓流判满：`batt:` 日志原来只走串口，日常不连电脑采不到放电曲线，连电脑又一直在充电。现在每分钟一行同时写串口和卡上 `batt.log`（带 RTC 时间戳、开机秒数、CX 充电状态原始码 cs），由独立低优先级任务写卡，不卡主循环跟手；卡不在、未挂载或 USB 读卡让出期间静默跳过，逐行开关文件，拔卡/断电最多丢最后一行。另据真机数据（0 mA 电流下 CX 仍报快充码、done 位不置位，原判满路径永不触发，充电显示卡在 97.9%）新增涓流判满：充电中 ≥4360 mV 且正向电流 <25 mA 持续 5 分钟即对齐 100%，负载浪涌（负向电流）不清确认窗口。后续据卡上日志校准 OCV 曲线锚点。
  Battery calibration log on the TF card plus taper-full: the `batt:` line used to be serial-only, so daily use off-computer captured no discharge data while staying plugged in captured nothing but charging. It now appends once a minute to both serial and `batt.log` on the card (with an RTC timestamp, uptime seconds and the raw CX charge-status code cs), written by a separate low-priority task so the loop never stalls mid-drag; a missing or unmounted card (including USB export) skips silently and the per-line open/close loses at most the last line on removal or power loss. On-device data also showed the CX still reporting the fast-charge code at 0 mA with the done bit clear—so the original done path never fired and the charge display stuck at 97.9%—adding taper-full: on the charger at ≥4360 mV with positive current under 25 mA for five minutes the percent aligns to 100%, and load surges (negative current) never clear the confirmation window. OCV anchors get calibrated from the card log later.

- 阅读正文：修复收起快捷栏、关闭「阅读设置」或收起底部工具栏后同一页文字一半深一半浅——被白色面板/遮罩/工具栏盖过的字在收起时多走了一轮「擦白 → 推黑」，比没被盖过的字更饱和（实拍墨量高约 22%），单次还原追不平。现在收起时整屏用归一表再走一遍：任意起点的字芯都是同样的擦白 18 相加推黑 18 相，全页落在同一墨度；关闭面板会有一次整体刷新（文字短暂隐去再回来）。首页、书架等页面收起快捷栏同样走这次整屏归一，纯文字页保留原厂的灰阶相等对，抗锯齿边缘一起归一，灰卡与封面不受影响。设置阅读背景后同样归一（此前基底会随背景换成相数更少的派生表，守卫不满足后归一被静默跳过）；背景页改用一张把非黑相等对保持的变体，整屏 pass 只重驱字芯，背景图片灰阶不被重驱。
  Reader text: fixed one page showing half-dark and half-light text after closing the quick layer, the Reading Settings sheet or the bottom toolbar—the glyphs the white sheet, shade or toolbar had covered ran one extra erase-then-black cycle and ended more saturated than the rest (about 22% more ink in a device photo), which no single restore can match. Closing now runs one whole-screen normalize pass: every glyph core gets the same 18 erase phases plus 18 black phases from any start, so the whole page lands on one ink level; a close now costs one full-page refresh (the text briefly blanks and returns). Closing the quick layer on the home, shelf and settings pages uses the same whole-screen pass; plain-text pages keep the vendor gray equal pairs so antialiased edges normalize too, leaving gray cards and covers unaffected. With a reading background the pass now runs as well—the base used to follow the background into the shorter derivative, the guard failed and normalization was silently skipped. Those pages take a variant that holds the non-black equal pairs, so the pass re-drives only glyph cores and never the picture grays.

- 设置：撤下「升级和恢复 → BOOT 刷机」入口，该组只保留保存与恢复、屏幕修复；BOOT 页面代码保留，需要时加回一行即可恢复。
  Settings: the Maintenance group no longer lists BOOT flashing, keeping only backup/restore and panel repair; the BOOT page code stays and one row restores the entry.

- 底栏：点击切换主页时，被点的图标不再放大、下移再复位（此前读成图标跳动）；现在图标、文字与选中横条全程静止，点击只负责切页。返回键的按下反馈保持不变。
  Footer: tapping a root tab no longer enlarges, shifts and settles the tapped icon (previously read as the icon jumping); icons, labels and the selection marker now stay completely still and the tap only navigates. Back-button press feedback is unchanged.

- 设置：个性化功能新增「触摸震动」入口（状态栏签名下面），页内包含开关与弱/中/强三档强度（35/50/70 毫秒，默认开启弱档）。单指按下沿震一记；多点、触摸读错和阻塞回放的帧不震；打开开关或改档即时震一下作预览。锁屏、浅睡和关机前强制关断，浅睡期间马达不会残留通电；无马达的旧板显示「不支持」。配置备份升到 v28，旧备份回落开启弱档。
  Settings: personal settings gain a Touch vibration entry below the status-bar signature; its page holds the switch and weak/medium/strong intensity steps (35/50/70 ms, on and weak by default). One pulse fires per single-finger press edge; multi-touch, read errors and replayed frames stay silent; enabling the switch or changing the step previews a pulse. The motor is forced off before lock, light sleep and power-off so it never stays powered through sleep, and boards without a motor show "Unsupported". Backups move to v28; older backups fall back to on and weak.

- 设置：个性化功能在触摸震动下方新增「阅读背景」入口（此前已从字体设置移除）。列表列出 TF 卡 `pictures` 的 JPG/PNG（每页 7 张、上下滑动翻页、当前背景带选中圆点），点开进入预览页：上下滑动换图，解码在后台分步完成，点预览图弹「使用阅读背景？」确认后生效，底部一键「关闭阅读背景 · 恢复白底」。交互与壁纸选择一致；阅读器内只保留背景绘制，旧选择面板删除；设置项与备份沿用原有存储，无新版本号。
  Settings: personal settings gain a Reading background entry under Touch vibration (after the earlier removal from the font sheet). The list shows TF-card `pictures` JPG/PNG files (seven per page, swipe to page, dot on the active one); tapping opens a preview where swipes change the image, decoding runs in the background, tapping the picture asks for confirmation before applying, and one button clears back to white. The flow mirrors the wallpaper picker; the reader keeps only background painting with its old panel deleted, and the stored setting and backup stay as they were with no version bump.

- 阅读背景提速与水波纹恢复：设置背景后翻页不再同步排版——下一页预渲染对背景页恢复启用（缓存命中路径本就整屏混合背景，正确性不变）；水波纹门改用「稳定文字帧」，背景页同样走水波纹。混合改按面板序一次字节遍历查表完成（单次 392ms → 39ms，输出逐位不变）。背景页改用派生波形表（37 相裁至 29 相，to=0 增黑保留）：深色对剥离前导擦白、迁移变为单调变暗；未变化像素在条带扫过时保持——原厂表除纯白外所有相等对都带完整擦白+推黑，整带标脏会把整块深色图形推一遍白黑往返，这就是黑闪的根因。实测带背景整页翻转约 1.12s → 0.57s，深色图形不再闪黑；白底页波形不变。
  Reading-background speedup and ripple restored: with a background set, turns stop typesetting synchronously—next-page prefetch admits background pages again (the cache-hit path already blended the background, so correctness is unchanged)—and the ripple gate now uses a stable-text-frame flag so background pages ripple too. Blending became one panel-order byte pass through a lookup table (392 ms → 39 ms per turn, bit-identical output). Background pages ride a derived waveform table (trimmed 37 → 29 phases, to=0 boost kept): dark-pair transitions drop the leading white erase and darken monotonically, and unchanged pixels hold while bands sweep—in the vendor table every equal pair except pure white carries a full white-then-black run, and dirtying whole bands drove every unchanged pixel of a dark graphic through it, which was the black flashing. Measured turns drop from about 1.12 s to 0.57 s with dark art holding quiet; white pages keep their original waveform.

- 排版设置：边距拆成上/下/左/右四项独立留白，排版面板内按 2×2 排列，各自 0～96 像素、6 像素一档（默认左右 24、上下 0），旋钮直接显示方向与像素，端点仍可逐档微调。旧单一边距升级为左右同值、上下 0，横排观感不变；竖排也改为四向统一留白，原来作用于上下的边距需要重新设置。正文矩形、页首插图、通栏插图和悬挂标点都按真实右边距计算，非对称边距不再让标点冲出屏幕或让整屏插图偏移。配置备份升到 v29。
  Layout: the margin becomes four independent top/bottom/left/right insets in a 2x2 block, each 0–96 px in 6 px steps (left/right default 24, top/bottom 0), with the direction and pixel value on the knob and endpoint stepping retained. An older single margin migrates to equal left/right with zero top/bottom, leaving horizontal text unchanged; vertical text now uses the same four screen-side insets, so its previous top/bottom spacing must be set again. Body geometry, page-start and full-bleed illustrations and hanging punctuation all use the real right inset, so asymmetric margins no longer push punctuation off-screen or shift full-bleed art. Backups move to v29.

- 阅读背景：修复近白纸面的背景图（例如纸色约 250 的 JPEG）翻页后正文发虚——4-bit 量化把这类纸色抖动成 14/15 灰点，翻页时这些灰点走的是重裁剪的灰阶行，字形边缘墨度不匀。现在解码后先做白点归一：≥240 的像素归纯白，纸面落在面板白（15），装饰灰阶原样保留；背景预览与正文共用同一结果。
  Reading background: fixed soft text after turns on near-white artwork (for example a JPEG with a paper tone around 250)—4-bit quantization dithered that paper into 14/15 specks, and turns drove those specks through the deeply trimmed gray rows, leaving uneven glyph edges. Decoded pixels at or above 240 now snap to pure white, so paper lands on the panel white (15) while decorative grays pass through; the preview shares the same result.

- 电量：按卡上 `batt.log`（12.3 小时、499 行）校准两处实测参数。充满平台实测 4374–4385 mV（289 点、|I|≤15 mA，中位 4379、免载最高 4385），原曲线顶锚点 4400 mV 不可达，充满时只能算到 97.9%，再由涓流判满硬拉到 100%（2% 跳变）；顶锚点改为 4380 mV，充满平台直接读 99.2–100%（中位 99.8%），判满只补最后 0–1%。电池内阻由估值 100 mΩ 改为实测中位 180 mΩ（13 次充放电切换，区间 160–216），拔掉充电器第一分钟不再瞬间掉 3%，阅读中 90–100 mA 放电时显示也高约 2%、更贴近实际耗电。日志最深只到 4.30 V，低段锚点维持原值，等一次完整放电再校准。
  Battery: two measured parameters are calibrated from the card's `batt.log` (12.3 h, 499 lines). The measured full plateau is 4374–4385 mV (289 samples, |I| ≤ 15 mA, median 4379, 4385 unloaded max), so the old 4400 mV curve anchor was unreachable: a full pack read 97.9% and the taper-full snap pulled it to 100%, a 2% jump. The top anchor is now 4380 mV, so the full plateau reads 99.2–100% from the curve (median 99.8%) and the snap only covers the last 0–1%. Pack resistance moves from an estimated 100 mΩ to the measured median 180 mΩ (13 charge/discharge switches, range 160–216), which stops the first minute off the charger from dropping 3% and raises the reading about 2% during 90–100 mA discharge, closer to the real draw. The log reaches only 4.30 V, so the low anchors are unchanged until a full discharge is captured.

- 充电：按原厂 Metalio 固件初始化 CX25601N。此前固件只读充电状态，充电参数完全靠上一次厂商固件遗留；换电池或掉电后会回落到芯片默认档，而且 DPDM 探测会把电脑 USB 口的输入限到 500 mA。现在开机写一次并回读：关 DPDM 探测、关看门狗，充电截止 4.4 V，充电电流 1 A（80 mA 一档，实际 960 mA），输入限流同 1 A，预充/终止码值和三个私有寄存器照抄原厂；充电期间不自动终止（原厂监控任务的稳态），所以 STATUS1 保持在恒压码 2，判满仍由涓流逻辑负责。启动日志会打印 `CX25601N part=0x02 ichg=960mA iindpm=1000mA vreg=4400mV`，插电脑时也能据此确认快充是否生效。注意原厂同样不做 DPDM 探测，插电脑 USB 口时按 1 A 取电；只有插墙充才能看到完整快充电流。
  Charging: CX25601N is now initialised the way the vendor firmware does it. Our firmware used to read only the charge status, so the charger kept whatever an earlier vendor build left in its registers; after a battery swap or power loss it fell back to chip defaults, and DPDM probing capped a computer's USB port at 500 mA. Boot now writes and reads back: DPDM probing and the watchdog off, 4.4 V regulation, 1 A charge (80 mA steps, so 960 mA effective), the same 1 A input limit, and the vendor's precharge/termination codes plus three private registers. Termination stays off while charging (the vendor monitor's steady state), so STATUS1 holds the CV code 2 and the taper-full logic still owns the 100% call. The boot log prints `CX25601N part=0x02 ichg=960mA iindpm=1000mA vreg=4400mV`, so fast charge can be confirmed even from a computer. The vendor also skips DPDM probing, so a computer USB port is asked for the full 1 A; the complete fast-charge current needs a wall adapter and a partly discharged pack.

- 设置：「升级和恢复」下面新增「电源管理 → 日志调试」开关，接管电量校准日志。默认关闭：不创建 `batt.log`、不打印 `batt:` 串口行，并省掉此前每分钟一次、关闭后无人消费的库仑计 I2C 读。打开后每分钟把一行写到 TF 卡根目录并同时输出串口；卡不在或正在 USB 读卡时自动跳过；开关立即生效，重启后保持。该项属于设备本地调试开关，不参与「保存与恢复」备份。
  Settings: a Power management → Log debugging switch under Upgrade & recovery now owns the battery calibration log. It defaults off: no `batt.log`, no `batt:` serial line, and the once-a-minute coulomb-counter I2C read that would have no consumer is skipped too. When on, one line per minute goes to the TF-card root and the serial console; a missing card or USB card export skips silently, and a change takes effect at once and survives a reboot. The switch is device-local debug state and is not part of Save & restore backups.

## 0.3.3-rc101 本地 / 2026-10-26

- 快捷栏：WiFi 与蓝牙圆钮支持长按 0.5 秒直达设置——WiFi 打开配网页（与设置列表的 WiFi 行同一入口），蓝牙打开「蓝牙翻页器」子页；短按仍只切换开关，按住时手指可在圆钮范围内轻微移动，抬手不再补一次点按。从其他页面长按进入时，这两个设置页左上角的返回回到来源页；从阅读正文离开的返回会直接回到原来的书与阅读位置（保留全屏状态），不再落到书架。
  Quick layer: holding the WiFi or Bluetooth circle for 0.5 seconds now opens its settings page—WiFi goes straight to provisioning (the same entry as the Settings list's WiFi row) and Bluetooth opens the Bluetooth turner sub-page. A tap still only toggles the switch, the finger may drift inside the circle while holding, and the release no longer adds a tap. When the hold starts from another page, the top-left back in either settings page returns to that origin; from the reading body it returns straight to the same book at the same position (full-screen state kept) instead of landing on the shelf.

- 阅读页底部留白：正文页底常空出 2–4 行白，看起来明明还能再放一段距加一行字。原因是分页按整行盒判定（48px 字号、130% 行距 = 62px），而字形只占约 48px，段末 31px 段距落在页底时也整段计入。现在页底只要求字形高度放得下：行盒尾部的行距悬在正文底之外（墨迹仍留在版心内），落在页底的段距压缩到剩余空间；分页、绘制和空闲预绘游标三处同步。74 页的样本重排为 69 页，其中 57 页的页底墨迹到进度条由 58/89/120px 收窄到 9–27px；行距、段距设置、插图与竖排不变，阅读线在页底最后一行不再画进页脚带。
  Reader page bottoms: pages used to end with two to four lines of blank space, enough to look like one more paragraph gap plus a line could fit. Pagination measured the whole line box (48 px text at 130% leading is 62 px) while the glyphs occupy about 48 px, and a paragraph's 31 px gap at the page bottom counted in full. The page bottom now only requires the glyph height: the box's trailing leading hangs below the body (the ink stays inside) and a gap that lands at the bottom shrinks to the space left; pagination, drawing and the idle prefetch cursor share the same state. A 74-page sample repaginates to 69, and on 57 of those pages the gap from the last ink to the progress bar narrows from 58/89/120 px to 9–27 px; line and paragraph settings, illustrations and vertical typesetting are unchanged, and the reading guide no longer spills into the footer band on a page's last line.

- 阅读背景翻页残影：设置阅读背景后翻页，旧页文字会留下成片淡灰字形。原因是背景页用的派生波形表在白列上被裁了两次——基底取原厂 18 拍，二次裁剪又削掉 3 拍，旧字从黑转白只剩 15 次白推；无背景页用的是主页水波纹的 24 拍，所以只有开背景时明显。现在派生表的 to=15 行改回主页水波纹已验证的 24 拍，按原形状（含中间两个保持相）右对齐搬入现有 29 相，不增加相数，翻页时长不变；其余迁移、黑闪保护与 to=0 增黑都不动。深色背景上黑字落到深灰的那类残留未处理（那是剥离前导擦白的取舍），本次只解决白纸/浅色纸上的淡灰字形。
  Reading-background turn ghosting: with a reading background set, the previous page's text left broad pale-gray glyphs. The background-page derivative had its white column trimmed twice—the base carries the vendor's 18 pushes and the second trim shaves three more, leaving only 15 for black-to-white erases—while pages without a background use the main water table's 24, which is why only background pages showed it. The derived table's to=15 row now carries those verified 24 pushes, moved in right-aligned with its two interior holds inside the existing 29 phases: the phase count and turn timing stay the same, and every other transition, the black-flash protection and the to=0 boost are untouched. Black text landing on dark background tones still keeps the darker shadow (that is the leading-erase trade-off); this change covers the pale ghosts on white and light paper.

- 快捷栏：修复设置阅读背景后，在主页、书架、设置、文件管理等非阅读页收起快捷栏，墨色归一几乎不起作用——整屏归一表此前按「设置里是否存了阅读背景」选变体，于是这些页面拿到的是只重驱纯黑字芯、其余灰阶保持的版本，被半透明遮罩盖过的灰卡、封面、图标和文字边缘无法拉齐。现在改由页面自己报告画面里是否有需要保护的图片灰阶：只有阅读背景正文返回是，其他页面一律走完整归一；阅读页的归一行为保持不变。
  Quick layer: after setting a reading background, closing the layer on non-reader pages (home, shelf, settings, files) barely normalized the ink—the whole-screen table picked its variant from "is a reading background stored", so those pages got the one that re-drives only pure-black glyph cores and holds every other gray, leaving the gray cards, covers, icons and glyph edges the translucent shade had covered misaligned. The page now reports whether the frame carries picture grays worth protecting: only a background reading body says yes, every other page takes the full pass, and the reader's behavior is unchanged.

- 设置页：列表最底部新增一行居中的版本号与固件构建日期（形如「版本 0.3.3-rcNN · 构建 2026-10-26」），随列表滚动到最后露出，参数取应用描述里的版本与 UTC 编译日期；滚动上限随之按该行高度计算，系统字号放大后也能完整露出。
  Settings: the list now ends with a centred version and firmware build date line (e.g. "版本 0.3.3-rcNN · 构建 2026-10-26"), revealed by scrolling to the bottom. It reads the app description's version and UTC build date, and the scroll limit accounts for the line's scaled height so a larger system font still clears the nav bar.

- 蓝牙翻页器：修复多报告设备「连上但按键没反应」——描述符遍历此前以 HID 服务末尾为范围，会把后面特征的 CCCD 也收进来，后一条覆盖前一条，最终所有输入报告都订阅到最后一个 CCCD（常见是 Boot Keyboard），Report 模式下通知永远不来；现在按下一个特征的定义句柄定界，并只记录第一条 CCCD。Report Map 改用分段读（read_long），MTU 23 下不再只读前 22 字节；键盘页与消费者页共存时先试键盘字节、为零再回退到消费者页，内置映射补上上一曲/下一曲；对端丢失配对后重新配对会删掉旧 bond 再重试，明文翻页器掉线后也能自动重连；设置页「已连接」改按地址判断。S31 上不存在或已改名的 BLE Kconfig（BLE_MAX_ACT、ADV_DUP_FILT_MAX、ACL_BUF_COUNT、TASK_STACK_SIZE）已清理。
  Bluetooth page-turner: fixes "connected but no key" on multi-report devices—descriptor discovery used the HID service end as its range and collected later characteristics' CCCDs, each overwriting the previous, so every input report subscribed to the last CCCD (usually Boot Keyboard) and report-mode notifications never arrived; each pass is now bounded by the next characteristic's definition handle and only the first CCCD is kept. The Report Map is read in chunks (read_long) instead of the first 22 bytes at MTU 23; when keyboard and consumer pages coexist the keyboard byte is tried first and the consumer page is the fallback, the built-in map gains previous/next track, a lost peer bond is dropped and re-paired, plaintext remotes reconnect after a drop, the Settings "connected" row compares addresses, and the BLE Kconfig items that no longer exist or were renamed on S31 are cleaned up.

- 蓝牙翻页器：修复「仅浅睡」锁屏后解锁立即重启——浅睡期间 BTDM 控制器一直活着，醒来后自动重连的第一条 HCI 命令触发控制器内部链表断言（`ble_hw.c:506`）。现在锁屏进浅睡前先拆蓝牙栈（保留自动重连目标），解锁后重新起栈并自动连回；拆栈失败时保持唤醒轮询，绝不把活动控制器送进浅睡。代价是解锁后翻页器需要几秒重连。
  Bluetooth page-turner: fixes an immediate reboot after unlocking a light-only lock—the BTDM controller stayed alive across light sleep, and the first HCI command of the post-unlock reconnect tripped an internal controller linked-list assert (`ble_hw.c:506`). The stack is now torn down before light sleep (keeping the auto-reconnect target) and restarted with an automatic reconnect after unlock; if the teardown fails the device stays awake instead of sending an active controller into light sleep. The turner takes a few seconds to reconnect after unlock.

- 蓝牙翻页器：解锁后的重连明显加快——连接后主动交换 ATT MTU（23 → 185），Report Map 从最多 35 个往返降到 5 个；解码提示（键盘页/消费者页）按遥控器缓存在 NVS，重连直接跳过 Report Map 读取。同时声明支持 LE Secure Connections，要求 SC 的新遥控器可以配对，老遥控器仍走 Legacy。扫描改用 BLE 5 扩展广播，只发扩展广播的设备也能出现在列表里，Legacy 广播同样可见。暴露多个输入报告的遥控器，其订阅现在也能跨重启保留。
  Bluetooth page-turner: reconnecting after unlock is noticeably faster—the link now exchanges ATT MTU (23 → 185), cutting the Report Map read from up to 35 round trips to 5, and the decode hints (keyboard/consumer page) are cached per remote in NVS so a reconnect skips the Report Map read entirely. It also advertises LE Secure Connections, so newer remotes that require SC can pair while older ones still use Legacy pairing. Scanning now uses BLE 5 extended advertising, so devices that only advertise in extended form appear in the list while legacy advertisements remain visible. Remotes exposing several input reports now keep every subscription across a reboot.

- 蓝牙翻页器：连着翻页器时进「扫描」页，现在会先断开当前连接再开始扫描。此前扫描与连接抢同一个控制器，扫描请求被直接忽略，页面只会显示空列表。离开扫描页后点一下已配对设备即可重新连接。
  Bluetooth page-turner: opening the scan page while a remote is connected now drops the current link first and then scans. Previously the request was ignored because an active link owns the controller, so the page stayed empty. After leaving the scan page, tap the paired device to reconnect.

## 0.3.3-rc101 本地 / 2026-10-27

- 电量校准：用卡上日志的回充对账修正 OCV 曲线中段锚点。新日志含一次满电深放 8.4 小时到 4075 mV 再回满的完整循环，从 4092 mV 静置回满只净充 346 mAh、另一次夜间放电回充 ~93 mAh，两条回充腿与放电库仑积分互相一致（容量按规格 2050 mAh），据此把 4300/4200/4100 mV 锚点从 900/800/680‰ 修到 955/875/790‰——原中段为无数据推测值，偏低 6–11%，深放电底点的显示从 68% 修到 77%（BQ 内部库仑计当时读 85%）。内阻复核 14 次切换中位 206 mΩ、1 A 大步 175–192 mΩ，维持 180 mΩ。4.09 V 以下仍无放电数据，低段锚点不动、不外推，等一次放电到关机。对拍测试新增回充锚点断言；`test_pmu_battery.py` 补上坏 SDK 的编译回退并剥掉本机会挂的 ASan。
  Battery calibration: the mid-curve OCV anchors are corrected by refill accounting from the card log. The new log contains a full cycle—full pack discharged 8.4 h to 4075 mV then recharged—so refilling from 4092 mV resting took only 346 mAh net, a second overnight leg took ~93 mAh, and both refill legs agree with the discharge coulomb integral (capacity per spec, 2050 mAh). The 4300/4200/4100 mV anchors move from 900/800/680‰ to 955/875/790‰: the old mid anchors were guesses and read 6–11% low, so the deep-discharge floor now shows 77% instead of 68% (the BQ internal gauge read 85% there). Pack resistance re-checked at 14 switches (median 206 mΩ, the three 1 A steps 175–192 mΩ); 180 mΩ stays. Below 4.09 V there is still no discharge data, so the low anchors are untouched and not extrapolated—awaiting one full discharge to shutdown. The replay test gains refill-anchor assertions, and `test_pmu_battery.py` gets the broken-SDK compile fallback with the hanging ASan stripped.

- 阅读正文：EPUB 里的脚注标记（如 `[1]`）可以点开。章节解析时记下正文内链区间和本章锚点，绘制时记下每条链接的落笔矩形；点中标记弹出「注释」弹层，显示锚点所在段落并去掉段首的 `[n]` 返回标记，点任意处或按任意键收起。脚注标记常落在正文右三分之一，所以内链判定先于翻页热区；预绘的下一页同样记录命中表，翻页后立即可以点开。跨章链接和站外链接不展开，仍按原翻页规则处理。
  EPUB reader: footnote markers such as `[1]` are tappable again. A chapter parse now records body link spans and in-chapter anchors, and drawing records each link's rectangle; tapping a marker opens a Note sheet with the anchored paragraph (the leading `[n]` backlink is dropped), dismissed by any tap or key. Markers often sit in the right third of the body, so the link test runs before the page-turn zones, and the prefetched next page records its own hit table so a footnote is tappable as soon as the turn lands. Cross-chapter and external links are left alone and still follow the usual turn rules.

- 阅读正文：设置阅读背景后，精排 EPUB 里的插图改为跟随正文四向边距，不再整屏通栏。背景内框是靠调边距对齐的，插图此前却按面板整宽缩放，图文混排页的图片会压到背景边框上、左右越出正文栏；现在不开背景时通栏效果不变，开背景时插图在正文栏内等比缩放居中，页首图也与正文上沿对齐。
  Reader: with a reading background set, illustrations in reflowed EPUBs now follow the four body margins instead of bleeding to the full panel. The background's inner frame is aligned by those margins, but illustrations were still scaled to the panel width, so mixed text-and-image pages crossed the frame and ran past the text column. Full-bleed behaviour without a background is unchanged; with one, illustrations aspect-fit and center inside the body column, and a page-start image aligns with the top of the text.

- 阅读正文：修复开着阅读背景和水波纹翻页时，插图页残留上一页文字轮廓的问题。插图差分此前沿用背景派生波形表，那张表为压黑闪剥掉了深色对的前导擦白，旧字落到图片灰时从黑轨直接进灰尾，落点和纸灰起点不一致，字形就以低对比度留在图上，而且等值对保持让错误一直保留到周期全刷。插图页（含换章那一帧的整屏差分）现在改用一张独立派生表：深色对保留原厂擦白，等值对仍保持；纯文字页仍用背景派生表，普通刷新与水波纹节拍不变。
  Reader: fixed the previous page's glyph outlines staying on illustration pages when a reading background and the ripple turn effect were both on. Illustration turns reused the background waveform derivative, which strips dark pairs' leading erase to cut black flashing; old glyphs then settled on an image gray from the black rail instead of the white rail, landed differently from paper-origin pixels and kept their shape at low contrast—and holding equal pairs kept the error until the periodic full refresh. Illustration pages, including the whole-screen differential on a chapter change, now take their own derivative that keeps the vendor erase on dark pairs and still holds equal pairs; text pages keep the background derivative, so ordinary refreshes and ripple timing are unchanged.

- 阅读正文：修复插图页之间翻页后正文空白处出现细小横线（上一页文字的行边）的问题。上一版插图页改用整页翻页表，那张表保留原厂相等对，即未变化像素也走一遍完整擦白加推黑；差分的脏区膨胀正好会把变化像素附近那圈未变化像素带进来，背景纸灰被这样推一遍就在字行上下留下一圈淡线，下一帧又因“没有变化”而被跳过。插图页新表把相等对全部置保持，膨胀带上的像素不再被驱动；深色对的原厂擦白仍然保留。
  Reader: fixed the thin horizontal lines (edges of the previous page's text rows) that appeared in blank body areas after turning between two illustration pages. The previous build gave illustration pages the full page table, which keeps the vendor's equal pairs—unchanged pixels still make a complete erase-then-black trip. A differential's dirty-region dilation pulls exactly that ring of unchanged pixels in, so the background paper was pushed through the trip and a faint line stayed above and below each old text row, then got skipped as "unchanged" on the next frame. The illustration table now holds every equal pair so dilation drives nothing extra, while dark pairs keep the vendor erase.

## 0.3.3-rc100 本地 / 2026-10-24

- 待机方式更名并恢复彻底断电：「先浅后深」更名为「彻底关机」，锁屏出图后不再有十分钟浅睡阶梯，立即按原厂方式打电源键脉冲彻底断电（f326902 的反复脉冲重试），主机与面板断电；此后开机需长按电源键约 1 秒。锁屏画面仍保留，开机回到锁屏与阅读位置（有密码时先验证）。「仅浅睡」与菜单「关机」行为不变。
  Standby style renamed and back to full power-off: "先浅后深" (staged) becomes "彻底关机" (full off); locking no longer stages ten minutes of light sleep—the device pulses off factory-style right after the lock art (f326902's repeating pulse trains), cutting host and panel. Booting afterwards requires holding the power key for about one second. The lock image is retained and a boot returns to the lock face and reading position (PIN verification first when enabled). Light-only and the power menu's 关机 are unchanged.

- 电量显示稳定化：修复翻页/刷屏时状态栏电量 ±1% 乱跳（此前是瞬时电压线性换算直出显示）。Metalio 电量现改为在 BQ27220 电压/电流之上滤波：负载补偿、EMA 平滑、4.4 V 分段 OCV 曲线、放电只降/充电只升的单向步进、1% 死区与 10 s 限速；充电完成对齐 100%，电量每 5 分钟和充电状态切换时持久化到 NVS，深睡唤醒显示连续。主页/电源页取整与状态栏统一为四舍五入。每 60 s 输出一条 `batt:` 校准日志（含 BQ27220 内部库仑计 SOC 对比读数），后续据此校准曲线锚点。
  Battery display stabilization: the status bar no longer jumps ±1% across page turns and refreshes (the percent used to come straight from an instantaneous linear voltage mapping). On Metalio the percent is now filtered on top of the BQ27220 voltage/current: load compensation, EMA smoothing, a 4.4 V piecewise OCV curve, one-direction stepping (falls only on discharge, rises only on charge), a 1% deadband and a 10 s rate limit; charge-done aligns to 100%, and the percent persists to NVS every 5 minutes and on charge transitions so deep-standby wake stays continuous. The home and power pages round like the status bar. A `batt:` calibration log line every 60 s (including the BQ27220 internal coulomb-counted SOC for comparison) feeds later curve-anchor calibration.

- WiFi 传输提速：对齐原厂 Metalio S31 无线实现——b/g/n 只强制 STA，热点保留默认协议；TX 功率上限（≈12 dBm）与关省电改到 esp_wifi_start() 之后设置，此前写在 start 前从未生效，满功率触发前端饱和重传；SDMMC 为 S31 增加 4 KiB 内部 DMA 中转缓冲（PSRAM 数据按 8 扇区分块搬运）；lwIP/WiFi 编译配置对齐原厂（TX BA 窗口 64、静态 RX/TX 6、缓存 TX 4、IRAM 优化、管理缓冲 32）。TCP 窗口实测后保持 32 KB：负载 RTT ~30 ms 下稳定 ~865 KB/s，64 KB 会冲爆接收缓冲塌缩到 ~380 KB/s。实测 16 MB 上传从 ~113 KB/s 提升到 ~810–907 KB/s。
  WiFi transfer speedup by aligning with the vendor Metalio S31 radio implementation: b/g/n is forced on STA only while the hotspot keeps the default protocol; the TX-power cap (≈12 dBm) and power-save disable move after esp_wifi_start()—the earlier pre-start calls never took effect and full power caused frontend-saturation retries; SDMMC gains a 4 KiB internal DMA relay for S31 (PSRAM data staged in 8-sector chunks); and the lwIP/WiFi build settings match the vendor build (TX BA window 64, static RX/TX 6, cache TX 4, IRAM optimisation, management buffers 32). The TCP window stays 32 KB after measurement: ~865 KB/s stable at the ~30 ms under-load RTT, while 64 KB overflows the RX buffers and collapses to ~380 KB/s. Measured 16 MB uploads improve from ~113 KB/s to ~810–907 KB/s.

- 快捷栏前光重排：下拉默认仍是 Wi-Fi、蓝牙、刷新、前光四个圆钮；点前光开灯后圆钮下方才展开两条横向胶囊滑杆——控制中心式，直接浮在遮罩上，无卡片、无文字、无旋钮，亮度用半填充圆图标、色温用温度计图标放在胶囊内左端，白色填充从左缘生长并无极滑动；再次熄灯即收起，层高随之伸缩。亮度图标与前光开关的太阳明显区分。滑杆配套开机生成的专用拖动波形表：拖动帧与松手提交都走 8 帧近似表保跟手，松手不再「洗白→落灰」定稿，消除左滑暴露带上的整段白闪；近似落灰由下次开层的整层 GL16 归一。
  Quick-layer frontlight redesign: pulling down still shows the four circles (Wi-Fi, Bluetooth, refresh, frontlight); switching the light on expands two horizontal capsule sliders below the circles—control-centre style, floating on the shade with no card, text or knob; the half-filled-circle (brightness) and thermometer (colour temperature) icons sit inside the left end, a white fill grows from the left edge with continuous dragging; switching off collapses them again and the layer height follows. Brightness never reads like the sun switch. A boot-generated drag waveform table serves the sliders: drag frames and the release commit both use the 8-frame approximate table for tracking, and the release no longer whitens and settles, removing the full-band white flash on left swipes; the approximate landing is normalized by the next layer-open GL16.

## 0.3.3-rc99 本地 / 2026-10-24

- USB 读卡两处修复：板端结束读卡后 TF 卡不再一直显示未挂载（后台 CMD13 掉卡检测在卡交给 USB 期间不再占用挂载状态机，退出后本机重挂可正常执行）；电脑安全弹出后磁盘不再几秒后自动重新挂载（MSC 组件锁存 SCSI 弹出并报告无介质，板端结束读卡后本机恢复）。
  USB card-reader fixes: ending USB on the device no longer leaves the TF card stuck unmounted (the background CMD13 health poll no longer claims the mount state machine while the card is exported to USB, so the post-USB remount runs), and a safe eject on the computer no longer remounts the disk seconds later (the MSC component latches the SCSI eject and TEST UNIT READY reports no medium until USB mode ends on the device).

- 待机「先浅后深」唤醒修复：脉冲断电后硬件只能长按开机，锁屏满 10 分钟不再断电，改为进入主机深睡——面板画面保留，短按电源键即重启回到锁屏与阅读位置（有密码时先验证）。菜单「关机」仍为彻底断电，长按约 1 秒开机，行为不变。
  Staged standby wake fix: after pulse power-off the hardware only boots on a long hold, so after ten locked minutes the device no longer powers down—it enters a host deep sleep with the lock image retained, and a short power-key press reboots back to the lock face and reading position (PIN verification first when enabled). The power menu's 关机 still cuts power completely and needs a ~1 s hold, unchanged.

- 电源菜单滑动关机修复：回放积压的触摸帧不再每帧各推一次扫描，而是合并滑块轨迹后统一推一次；此前一次扫描 55–90 ms 而触摸帧 5 ms 一帧，拖动时积压越滚越大，滑块越拖越落后。触摸笔迹用的 8 帧跟随 DU 面积差分同时固定走 FAST 扫描档，滑块跟手速度约为原来的两倍。
  Slide-to-off fix: replayed touch frames no longer push one scan each—the slider trajectory is merged and pushed once. Previously a scan cost 55–90 ms while touch frames arrived every 5 ms, so the backlog grew during a drag and the thumb fell further behind. The 8-frame FOLLOW DU used for touch ink also now always runs the FAST scan profile, roughly doubling slider tracking speed.

- 分区：nvs 移到 flash 末尾（0xFF8000，32 KB），内置书库分区缩 32 KB 让位。此后单文件整机镜像与网页完整刷机不再覆盖用户数据——设置、锁屏密码、阅读进度与阅读历史整刷后保留。采用新分区表的那次完整刷机仍会重置一次设置与进度，并把内置 /flash 书库格式化为空（存在里面的书要重新传输，TF 卡书籍不受影响）；OTA 与 TF 应用包升级不受影响。
  Partitioning: nvs moves to the end of flash (0xFF8000, 32 KB) and the internal library partition shrinks 32 KB to make room. Full single-image and web flashes no longer wipe user data—settings, lock PIN, reading progress and history survive a reflash. The one full flash that adopts the new layout still resets settings and progress once and formats the internal /flash library empty (books stored there must be re-transferred; TF-card books are untouched); OTA and TF app updates are unaffected.

## 0.3.3-rc99 本地 / 2026-10-23

- 设置：「关机睡眠」更名「待机方式」，电源菜单「关机」固定立即断电、不再跟随该设置；页内移除「彻底关机」，只留「先浅后深」与「仅浅睡」，已存档和备份里的彻底关机自动并入先浅后深，页底说明文字一并删除。
  Settings: the shutdown-sleep setting is renamed 待机方式 (standby style) and the power menu's 关机 always powers off immediately instead of following it; the page drops full-off, leaving staged and light-only, stored full-off values (NVS or backups) migrate to staged, and the bottom hint line is removed.

- 关机睡眠「先浅后深」：本机没有 PMU EN，锁屏满 10 分钟后不再永远停在浅睡，而是按原厂方式打电源键脉冲彻底断电，之后短按电源键冷开机并恢复锁屏与阅读位置；电源菜单「关机」的脉冲也改为反复重试直到断电，不再一轮失败就进深睡兜底。
  Shutdown style "staged": this board has no PMU EN, so after ten locked minutes the device no longer stays in light sleep forever—it pulses the power key factory-style and powers off completely; a short key press then cold-boots back to the lock face and reading position. The power menu's shutdown pulses also retry train after train until the rails drop instead of falling into deep sleep after one round.

- TF 卡：拔卡约 1.5 秒后判为掉卡，阅读页回退内建字体、目录提示读取失败；卡插回后 2 秒内自动重挂，字体和目录自行恢复，不用退出页面或重启。此前本机没有卡座 CD 脚，拔卡后设备一直以为卡还在，只能反复读超时。
  SD card: removal is detected about 1.5 s after the card leaves the slot, the reader falls back to the built-in face and listings report a read failure; a reinserted card remounts within about 2 s and the face and listings recover without leaving the page or rebooting. This board has no slot CD pin, so previously the device kept assuming the card was present and only produced read timeouts.
- 阅读页脚：章节名和页码固定使用同一种字体，带《》【】等标点的章节不再让整行页脚变小。
  Reader footer: the chapter name and page counter always share one face, so titles with 《》【】 no longer shrink the whole footer.

- 前光：亮度输出上限从原厂满输出的 30% 降到 20%，滑杆仍为 1–100%，已保存的亮度值不变。
  Frontlight: the brightness output ceiling drops from 30% to 20% of factory full output; the slider still spans 1–100% and saved values are unchanged.

## 0.3.3-rc98 本地 / 2026-10-23

- 配置备份升到 v27：「书内自带字体」「竖排正文」和关机方式三态（彻底 / 先浅后深 / 仅浅睡）现在都会保存并恢复。此前恢复后书内字体会回到开启、竖排会回到横排，「仅浅睡」会被恢复成「先浅后深」。旧备份仍可读取，缺少这些块时回落开启、横排和原扩展位。
  The settings backup is now v27: embedded book fonts, vertical body text and all three shutdown styles (full off, staged, light-only) are saved and restored. Previously a restore reset book fonts to on, vertical text to horizontal, and light-only to staged. Older backups remain readable and fall back to on, horizontal and the old extension bit when those blocks are absent.

- 设置页继续隐藏「升级和恢复 → 系统升级」入口：OTA 还没做好，暂不对外开放。联网与 TF 卡升级页面代码保留，重新开放时恢复这一行即可；其余三行随之上移，滚动上限同步收紧。底层升级链（5 MB 单槽镜像上限、menuconfig 中的升级源与额外下载域名）已就位但不可达。
  The System Upgrade row under Upgrade & Recovery stays hidden until OTA is ready. The online and TF-card update pages keep their code, so re-enabling needs only the row back; the remaining three rows move up and the scroll limit follows. The underlying release chain (5 MB slot image cap, manifest source and extra download origin in menuconfig) is in place but unreachable.

## 0.3.3-rc98 本地 / 2026-10-22

- 「按键控制」的动作列表新增「前光开关」：可绑给左/中/右键短按、中键长按、中键双击或上键单击，按一次开灯、再按一次关灯；开灯沿用下拉快捷栏记住的亮度与色温，没有前光的板保持不操作。列表从七行变为八行，行距相应收紧，底部提示与「返回」按钮位置不变；旧配置和备份里的动作编号不变。
  The key-control action list gains a frontlight toggle, bindable to any short key, the middle hold, the middle double press or the up key: one press lights it and the next turns it off, using the brightness and colour temperature the pull-down quick layer remembers, while boards without a frontlight keep it a no-op. The list grows from seven to eight rows at a tighter pitch, keeping the hint and Back button in place and leaving existing action numbers in saved settings and backups unchanged.

- 前光亮度上限降到原厂满输出的 30%：滑杆仍为 1–100%，已保存的亮度值不变，但拉满时的实际占空比只有此前的 30%，整条滑杆仍连续可调；亮度数字不再另设新档。
  The frontlight brightness ceiling drops to 30% of the factory full output: the slider still spans 1–100% and saved values are unchanged, but a full slider now drives 30% of the previous duty and the whole range stays continuously adjustable, with no new levels added to the saved value.

- 「按键控制」新增「上键单击」：机身顶部那颗实体上键在阅读正文里可设为上一页、下一页、阅读工具栏、切换全屏、返回主页、手动全刷或不操作，默认不操作，随配置备份保存。中键双击行改成与中键长按同高的整行样式；列表因此比可视区多一行，上下滑动整行查看，面板高度与底部「完成」按钮位置不变。
  The key sheet gains an up-key single press: the top-edge physical key can be mapped, in the reader body only, to previous/next page, the reading toolbar, full screen, home, a manual full refresh or no action (the default), and the mapping is part of the settings backup. The middle double-press row now matches the hold row's full-height style; the list therefore outgrows its viewport by one row and scrolls a whole row at a time, keeping the sheet height and the Done button where they were.

- 修复翻页后正文笔画偏灰：裁表时只砍了黑色饱和段，旧字擦白保留 18~24 次，新字推黑却只剩 13 次。普通翻页、整页阅读输出与水波纹现在都把「白底→黑」补回原厂 18 次推黑，占用各自的前导保持相，37 相和翻页时间不变；水波纹仍是 8 带、44 拍。
  Fix gray-looking text after a page turn: the trimmed table kept 18–24 erases for the old glyphs but only 13 black pushes for the new ones. Ordinary turns, full-page reader output and water turns now restore the vendor's 18 black pushes for white-to-black using each row's leading holds; the 37 phases and turn time stay the same, and water keeps its eight-band 44-tick turn.

- 调整两项默认值：无操作自动锁屏默认改为 5 分钟（仍可选 1/5/10 分钟或关闭），关机睡眠默认改为「仅浅睡」——关机后保留锁屏画面、不断电，按键或拿起返回关机前页面。只影响从未保存过该项设置的设备，已保存的选项保持不变；「关闭」现在会作为明确选择持久保存，不再被默认值覆盖。
  Two defaults changed: idle auto-lock now defaults to five minutes (1/5/10 or off remain selectable), and the power-menu shutdown style defaults to light-only, keeping the lock face and power so a key or pickup returns to the pre-shutdown page. Only devices that never saved these settings are affected; existing choices stay. An explicit "off" is now persisted and no longer overridden by the default.

- 面板高压轨（VCOM/VSH）现在无论前光是否点亮，每次刷新后都只保留 100 ms，空闲即断电，不再保留 8 秒。这保留了开灯时抑制灯带侧发灰的修复，也缩短了灭灯后轨带电的时间；代价是间隔超过 100 ms 的刷新会多约 80 ms 的 TPS65185 上电时间。
  The panel HV rails (VCOM/VSH) now stay up for only 100 ms after each refresh regardless of the front light, dropping on idle instead of holding for 8 s. This keeps the lit-side graying fix and shortens rail-on time with the light off; refreshes more than 100 ms apart pay roughly 80 ms more for the TPS65185 power-up.

- 下拉快捷栏的前光控制改为第四枚圆钮开关：开灯为黑底白太阳，熄灭为浅底黑太阳，与旁边图标同粗细；锁屏图标移出快捷栏。下方亮度、色温换成整行可点的大滑杆（14px 轨道、22px 滑块）并去掉右侧百分比，拖动实时生效、松手或点选才保存。滑杆拖动改为带脏区膨胀的差分刷新，减少圆圈上下边缘的浅残影，松手仍用 GL16 定稿。滑杆不再自动开灯，灯灭时只记录数值，必须点圆钮才亮；浅睡灭灯、唤醒恢复，无前光的板保留锁屏圆钮和四键布局。
  The pull-down quick layer now uses its fourth circle as the frontlight switch: black with a white sun when on and light with a black sun when off, matching the neighbouring glyph weight, replacing the lock icon. Brightness and colour temperature become full-row sliders with a 14px track and 22px knob without percentage labels, applied while dragging and persisted on release or tap. Slider drags now use dilated dirty masks to cut the light ghost along the knob's top and bottom edges, while release still settles with GL16. Sliders never turn the light on by themselves; with the light off they only record values and the circle must be tapped. Light sleep blanks the light, wake restores it, and boards without a frontlight keep the lock circle and the four-key layout.
- 修复「关机睡眠 = 仅浅睡」下锁屏唤醒后整个主页刷新变慢的问题。浅睡唤醒过程会把 CACHE_L1 的总线仲裁/时钟门控等四个配置寄存器清零而系统不恢复，喂行产能下降约 20%，20 MHz 扫描第一刷欠载后像素钟被永久降到 12 MHz。现在每次浅睡醒来把开机快照的配置写回，唤醒后主页刷新保持 20 MHz 原速。
  Fix the whole-home-page slowdown after a lock wake under the light-only shutdown style. The light-sleep wake path clears four CACHE_L1 configuration words (bus arbitration and clock gating) that nothing restores, cutting the EPD feed capacity by ~20%; the first 20 MHz scan underruns and the pixel clock stays at 12 MHz. The snapshot is written back after every wake, keeping post-wake refreshes at full 20 MHz speed.

- 暂时隐藏设置页“升级和恢复”里的“系统升级”一行：联网升级流程还没改好，先不开放入口。升级页面（TF 卡与联网）代码保留，重新开放时恢复这一行即可；其余三行位置随之上移，滚动上限同步收紧。
  Temporarily hide the System Upgrade row under Settings → Upgrade & Recovery until the online update flow is finished. The upgrade page code (TF card and online) is kept, so re-enabling only needs the row back; the remaining three rows move up and the scroll limit follows.

- 阅读设置「按键控制」的示意图改为与机身实体顺序一致：最左是圆圈中键，中间是左键，右侧是右键；点击卡片也按新位置进入对应功能列表，三键原有映射不变。
  The key-control diagram now follows the physical key order: the left circle is the middle key, the middle bar the left key, and the right bar the right key. Tapping a card opens that key's action list, and the existing mappings are unchanged.

- 修复「按键控制」动作列表下方选项点错的问题：列表绘制按 88 像素排、命中按 94 像素算，越靠下越容易点到上一行；七行现在共用同一组 94 像素矩形。
  Fix the key-action list firing the wrong row: painting laid the seven rows out at an 88 px pitch while hit testing used 94 px, so lower options drifted. Both now share the same 94 px rectangles.

- 「按键控制」在「中键双击」下方新增「音量键翻页」开关，默认关闭。开启后只在阅读正文响应侧键：音量+上一页、音量−下一页，打开设置面板或回到书架时不翻页；一次按下翻一页。开关写入 NVS，配置备份格式升到 v25，旧备份恢复后保持关闭。
  A volume-key page-turn switch now sits under the middle double press in key controls and is off by default. When on, it only acts in the reader body: volume-up turns back, volume-down forward, one page per press, and panels or the shelf stay inert. The setting is stored in NVS; the settings backup format moves to v25 and older backups restore it off.

- 阅读设置去掉「电源键翻页」这一项，七项按四行双列重排；「按键控制」的说明由「自定义三键」改为「自定义按键」，并移除中键长按下方的操作提示。底层电源键翻页设置和已保存的值保持不变，旧备份仍可正常恢复。
  Remove the power-key page-turn row from reading settings and reflow the remaining seven items across four two-column rows. The key-control summary now reads “自定义按键”, and the hint below the middle-key hold row is gone. The underlying setting and saved values are unchanged, so older backups still restore.

- 改进「通过 WiFi 对时」：临时连接期间关闭 WiFi 省电并等 120 ms 再连接；短暂掉线会在 15 秒内自动重连最多三次；NTP 等待从 12 秒延长到 20 秒，让 lwIP 在首个服务器无响应后有机会尝试下一个；NTP 服务器首选阿里云固定地址，绕开设备侧域名解析。
  Improve “sync over WiFi”: disable WiFi power save for the temporary link and give the hosted radio 120 ms before connecting, reconnect up to three times within 15 s after a transient disconnect, and extend the NTP wait from 12 s to 20 s so lwIP can try the next server after the first stays silent. The NTP server list now starts with Aliyun's fixed address to bypass device-side name resolution.

## 0.3.3-rc97 / 2026-10-18

- 修复锁屏设置读取失败被缓存整个开机周期、导致未设置密码的设备也显示四位密码的问题。读取失败或记录损坏时直接进入系统，不再用密码界面阻拦启动；不创建默认密码、不清空原记录。能正常读取的已设密码仍需验证；存储异常期间密码保护暂时不生效，恢复后可重新读取。
  Failed or malformed lock-setting reads no longer block startup or create default credentials. Readable enabled PINs still require verification; protection is temporarily bypassed during storage faults, without erasing the saved record, and subsequent checks can retry.
- 开机在大缓冲与字体加载前读取小型锁屏设置，减少资源紧张造成的读取异常；只有成功读取才缓存结果。设置不可读时，密码设置页显示暂不可用，禁止覆盖旧密码。
  Read the small lock setting before large buffers/fonts and cache only conclusive results. Unavailable settings cannot overwrite saved credentials.
- 修复字间距点按仍沿用旧上限、反复跳到-11px的问题，点按与拖动统一为-15至+15px，每次1px；重排失败恢复原间距和阅读位置，不保存错误值。段距两端按钮按现有11档逐级调整，可到100%。
  Tracking taps and drags share the complete -15 to +15px range in 1px steps. Failed reflow restores the original spacing and position without saving an incorrect value. Paragraph endpoint buttons follow all 11 existing stops through 100%.
- 目录书名与章节名统一使用标题绘制路径，沿用当前系统字体与整行字体来源，直接按目标字号生成抗锯齿；内建补充点阵保持原生大小，避免放大后模糊和笔画不均。当前章节的灰底正确混合字形边缘，不留下白边；正文抗锯齿设置保持。
  Directory book/chapter names share the title path with the active system face, stable whole-line font selection and native-size AA. Keep bitmap supplements at their native size, blend selected-row edges into gray and preserve the body's AA setting.
- 保留本地章节题头四款样式与 36–78px 八档字号，预览选择只改题头画法、不重排正文；阅读题头继续显示书名、时间和电量。
  Keep the local four chapter heading styles and eight 36–78px heading sizes; the preview picker repaints only the heading. The reader header keeps showing title, clock and battery.
- 阅读页中键双击可配置（默认不操作），中键长按与三键短按保持可配置；新开书沿用记住的全屏偏好，正文中央双击不再切换全屏。
  Middle double press is configurable (default off) alongside short and hold actions; new books follow the remembered full-screen preference, and center body double taps no longer toggle full screen.
- 保留本地自动对时、关机三态（彻底 / 先浅后深 / 仅浅睡）、触摸采样回放和字形失败诊断；设置备份兼容上游 rc96/rc97 与本地旧格式，边距统一夹到 48px。
  Retain the local auto time sync, three shutdown styles, touch sampling/replay and glyph diagnostics. Settings backups accept both upstream rc96/rc97 and the older local format, with margins clamped to 48px.
- 延续rc96的功能、分区与数据格式，保留用户配置、原密码记录和阅读进度。
  Retain rc96 features, partition/data compatibility, user settings, saved credentials and reading progress.

## 0.3.3-rc96 / 2026-10-18

- 设置页题头与列表外围改为纯白。普通模式进入设置及周期整理复用书架的同轮分区刷新：状态栏/题头柔和，列表白色仅向白擦除，保留内容灰阶；普通列表滑动使用柔和差分，手动全刷及原离散滚动保持。
  Keep settings chrome and list whitespace white. Ordinary entry/periodic cleanup share the shelf's simultaneous scan: soft status/title, directional white-list erasure and retained content grays. Use soft ordinary list differences while retaining manual full refresh and discrete scrolling.

- 启动记录改为按路径实际长度保存，兼容旧记录；NVS不足时只回收本模块旧检查点再尝试写入，保留设置、配对、书签与进度。普通版忽略不支持的缓存恢复入口，真实TXT/EPUB仍需成功记录后才允许开书，断电/解析失败保护继续生效。
  Store compact startup records by actual path length with legacy migration. On low NVS space reclaim only the old checkpoint; preserve settings, bonds, bookmarks and progress. Ignore unsupported cache recovery entries while requiring a successful checkpoint before TXT/EPUB parsing.

- 书架留白改为纯白；普通切页白像素仅向白擦除，封面/材质其余十五级保留校准序列，同步输出与手动全刷不变。
  Keep shelf whitespace white and whiten it directionally during ordinary transitions. Retain calibrated art grays, simultaneous output and manual full-refresh quality.

- 快捷蓝牙按钮支持未连接时再次关闭，并取消未启动的最近设备连接请求；快捷全刷直接输出当前合成画面，省去底页重绘和玻璃重新计算。
  Toggle quick Bluetooth off even before connection and cancel an unstarted recent-peer request. Quick full refresh presents the composed frame directly without re-rendering the underlay or regenerating glass.

- 普通书架改为同步分区输出：题头、封面/材质、分页与选中横条在同一轮逐相呈现，取消先刷上下再刷中间的三段等待；外侧保留柔和刷新，中间保留原校准灰阶及周期清理，临时PSRAM仅2048B，失败统一恢复。
  Present ordinary shelf chrome, calibrated artwork, pagination and the selection marker in shared scan phases instead of three serial regions. Retain soft outer output, calibrated middle phases and periodic cleanup, with 2048 temporary PSRAM bytes and common failure recovery.

- 阅读字号扩展为18–90px、每档1px，设置/备份/逐书进度统一范围；普通模式进入书架时，强灰阶整理只限封面与书架材质区域，状态栏、题头和分页采用柔和差分刷新。
  Extend reader type size to 18–90px in one-pixel steps across controls, settings, backups and per-book progress. Ordinary shelf entry confines strong grayscale settling to cover/furniture art; status, title and pagination use soft differential output.

- 下拉快捷菜单按下即给圆内局部反馈；Wi-Fi和蓝牙状态仅刷新对应圆键及必要的状态栏，不重画底页或重复生成玻璃。四圆键缓存关闭即释放。蓝牙按钮开启蓝牙并异步连接最近成功使用的翻页器，保留网络内存保护、重试退避和设置页关闭入口。
  Acknowledge quick-control presses locally; update Wi-Fi/Bluetooth keys and the necessary status strip without repainting the page or regenerating glass. Release four small key caches on close. Bluetooth enables and asynchronously reconnects the most recently successful turner, retaining network-memory guards, retry backoff and the Settings off switch.

- 顶部下拉快捷菜单及四个圆键采用与密码锁屏一致的液态玻璃，保留清晰底图和状态栏；三个主页模式统一灰阶，临时PSRAM绘完即释放，内存不足退回清晰实心控件。
  Share PIN-style liquid glass with the top quick panel and its four controls, keeping artwork/status crisp and grayscale in all main modes. Free temporary PSRAM after painting, with legible solid controls on low memory.

- 刷回普通版时，书架、首页续读和启动恢复会忽略其他书源的缓存清单，仅接收TXT/EPUB；保留原缓存与进度，避免不可读条目占用入口。
  When returning to the standard firmware, ignore other-source manifests in shelf history, Home continuation and startup requests; retain caches and progress while admitting TXT/EPUB only.

- 参考 MokuReader，为关机滑条、重启/取消圆钮和密码锁屏数字圆键增加液态玻璃效果；保留清晰页面底图，玻璃内加入模糊、边缘折射和亮边。滑动/输入只恢复局部4位灰阶缓存，关闭释放，内存不足退回实心控件。
  Adapt MokuReader liquid glass for power controls and PIN keys, retaining sharp background artwork with blur/refraction/highlights inside the controls. Input restores local packed grayscale snapshots without reblurring; close releases caches and low memory falls back to solid controls.

- 段距扩展为11档，25%位于中间；字间距扩展为−15..+15px、每档1px，0px居中。旧设置与备份按实际间距迁移，旧+16px收敛为+15px。
  Center 25% among eleven paragraph-gap stops and 0px among 31 tracking stops (−15..+15px). Migrate saved spacing by physical value, clamping legacy +16px to +15px.

- 非全屏阅读题头的收藏图标统一使用 AnimateIcons Bookmark / Bookmark Plus，已收藏显示加号书签，保留原收藏逻辑、触控范围与局部刷新。
  Use AnimateIcons Bookmark/Bookmark Plus for unsaved/saved header favorites while preserving favorite storage, hit targets and local presentation.
  修复图标生成器遗漏 SVG 线段数字坐标属性的问题，确保收藏后的加号实际显示。
  Preserve numeric SVG line attributes so the saved favorite's plus is visible in the generated artwork.

- 全部圆形返回键统一使用 AnimateIcons Circle Chevron Left，包括系统子页与阅读面板；按压保留原位和圆内反馈。
  System and reader-sheet circular backs share AnimateIcons Circle Chevron Left, retaining their positions and interior press tint.
- 底栏四图标恢复按下放大、随后回位；当前页、待切页和加载中的点击均有独立局部反馈，快速连点同一图标可重触发。
  Restore footer enlargement/settle on every contact, including active/pending/loading pages and rapid repeated taps, with local feedback independent of navigation.

- 阅读纯文字水波纹改用文字专用灰阶序列，保持未变化的抗锯齿边缘，并减轻灰色笔画先加深再回浅的过渡；保留最终字形、8条带、11090us节拍及原有全刷/插图/失败恢复。
  Text-only ripple shares the existing softened text phases, holding unchanged antialias pixels and reducing dark-edge transients while preserving glyph targets, eight bands/11090us, cleanup, image and recovery paths.

- 首行缩进微调支持长按加减，保持每步1px，松手后统一重排并保存；增加按钮下沿触控容错，排版空白不再误触“返回字体设置”，滑出、多指、锁屏与切页取消连续调整。
  Hold the indent adjustment buttons for 1px repeats, reflow/save once on release, tolerate lower-edge taps and keep layout gaps inert; leaving, multitouch, lock or navigation cancels repetition.

- 分页栏采用 AnimateIcons 的 Left/Right/First/Last 轮廓，首末页改为止点竖线；书架与文件管理分页栏上移，目录列表保留独立点击区域。
  Share AnimateIcons chevrons and first/last stop bars, raise shelf/file pagination and keep directory rows separate from paging targets.
- TF 目录移除“设为书籍目录”和“设为字体目录”；统一读取 books/fonts，旧设置与备份按固定目录使用，移动/改名/删除仍可用。
  Remove custom media-root actions and retain fixed books/fonts roots across legacy settings/restores, while preserving ordinary file management.

- 文件管理与书架共用“最前 / 上一页 / 下一页 / 最后”分页栏，书架保留书籍数量和页码；箭头加大，按键分页轮廓加粗，按压仅局部刷新。

- 阅读边距恢复九个真实像素档位，横排、竖排均保留中间值；首行缩进按正文汉字宽度计算，微调扩展至 ±100 px，并兼容旧配置。
- 字间距扩展至 −15～+15 px，每档 1 px，0 px 居中；旧设置与备份按实际间距迁移，旧 +16 px 就近收敛为 +15 px。
- 主页面左右按键切换页面，中键长按全刷；书架中键短按进入或退出分页控制，沿分页箭头轮廓描边并提供局部按压反馈，可随时切页中断。
- 阅读字体名称直接使用所选阅读字体预览；补齐“抗锯齿”的原生轮廓字形，界面标签不受正文抗锯齿开关影响。
- 字重微调继续每档 1/16 px，改用轮廓距离扩张，减少额外的灰色边缘。
- 自动全刷使用完整灰阶定稿的一次输出；手动全刷保留 rc90 路径。
- 深睡恢复复用已验证的实际锁屏帧，减少开机推屏和重复字体装载；旧缓存、缺卡或校验失败沿用安全启动路径。
- 锁屏新增透明 PNG 书封，以锁屏前的当前页面为底，保留 PNG 原画布比例和透明度，加入轻微悬浮阴影。推荐 684 × 1216 px，文件放入 TF 卡 pictures 目录。
- 白底 PNG 自动去掉与画布边缘连通的白色背景，保留封闭图案内部白色；已有透明度的 PNG 沿用原始透明通道。

## 0.3.3-rc95 / 2026-10-16

- 修复无变化差分或异常返回后像素驱动可能保持开启的问题：结束时统一释放驱动，短暂 I2C 失败有界重试，避免静止白底持续受驱动；不增加刷新或改变波形、电压与画质。
  Release pixel drive after unchanged differentials or failed output, retrying transient I2C errors without extra scans or changes to waveforms, voltage settings or image quality.

## 0.3.3-rc94 / 2026-10-16

- 阅读手动与周期全刷恢复 rc90 的原表 GC16 目标页输出，取消纯文字全刷前的独立擦白和后续文字补刷；保留普通翻页预绘、插图与水波纹设置。
  Restore rc90's original-table GC16 target-frame output for manual and periodic reader refresh, removing the separate white clear and text pass while retaining ordinary prefetch, images and ripple settings.
- 接入 PR #34：修正竖排阅读辅助线的位置和虚线节奏，补齐横线类符号旋转，并按字体原生全角步进对齐章节数字编号。
  Integrate PR #34 for vertical reading-guide placement and dash phase, dash-like punctuation rotation and chapter-number alignment using native fullwidth metrics.
- 接入 PR #32：RTC 轨迹记录开机各阶段耗时；首帧后停止开机标记，普通回首页不挤占记录。保留后台封面缓存加载。
  Integrate PR #32 boot-phase RTC timing, stopping boot marks after the first frame and retaining background cover loading.
- 接入 PR #33：设置 → 升级和恢复 → 屏幕修复，手动执行面板黑白恢复与当前页面整屏重建；结果提示随最终画面一次显示，输出失败保留恢复路径。正常阅读波形、底栏边界和分段滑动不变。
  Integrate PR #33 manual panel repair in Settings → Upgrade and recovery, restoring the complete current page with its final notice once and retaining output-failure recovery. Normal reading waveforms, navigation bounds and discrete scrolling remain unchanged.

## 0.3.3-rc93 / 2026-10-15

- 优先优化连续翻页：下一页预绘与当前页推屏重叠，连按复用已准备内容，未完成的目标接着绘制。保留正式 rc92 的阅读波形、插图、水波纹和周期清残影规则。
  Prioritize rapid turns with output-overlapped prefetch and partial-target reuse; retain rc92 reading waveforms, images, ripple and periodic cleanup.
- 首页和书架封面使用有界后台任务，优先复用 TF 成品缓存；过期结果按代次丢弃，书架整组显示。普通亚克力书架题头与内容合并为同一轮校准灰阶输出，固定底栏保持。
  Prepare covers in bounded background jobs, reuse processed TF caches, reject stale results and reveal shelf groups. Ordinary acrylic headers/content share one calibrated gray scan with fixed navigation.
- 拼贴锁屏复用小型书库索引，长章节行缓冲按实际文字增长，减少重复扫描与内存峰值；保留锁屏画质和低内存回退。
  Reuse a small collage catalogue and grow line scratch on demand, retaining artwork quality and low-memory fallbacks.
- 首页继续阅读整块可点，并增加封面反馈；「返回主页」按开书来源回首页或原书架分类页。资料卡头像后台准备，不再阻挡首页与设置操作。
  Make the entire continue card actionable with cover feedback, return to the opening's Home/shelf origin and prepare avatars without blocking page input.
- 改进 S3 RTC 计时，已联网每小时校准，离线空闲每六小时短连已保存 Wi-Fi 对时；休眠和其他网络操作可取消后台对时。
  Improve RTC timekeeping with hourly online and six-hour idle offline calibration; background sync yields to sleep and other network operations.
- 增大底栏触控范围并在首次接触时响应，优化普通主页切换、底栏边界和状态签名显示。三种主页模式的设置列表恢复分段滑动，取消此前跟手与惯性方案。
  Expand root-tab targets and respond on initial contact, refine ordinary transitions, navigation boundaries and status signatures. Restore discrete settings swipes in all main modes, removing follow/inertial scrolling.
- 底部上滑固定打开阅读工具栏，取消按键必须映射工具栏的限制；首行缩进控件增加字号与间距，返回字体设置统一黑底白字。
  Keep bottom-edge Tools independent of key mappings, enlarge and separate indent controls and use the matching dark return button.
- 官网网页刷机、TF、联网 OTA 与 Release 附件来自同一次正式构建；保留旧版本固定下载地址。
  Publish one build across web, TF, OTA and Release assets while retaining immutable older downloads.

## 0.3.3-rc92 / 2026-10-14

- 首页新增 Moon、Kehai、Cat、Dali、Asterisk、小屋时钟与最近阅读书夹，保留 IKEA PS1995；长按选择样式，单击打开对应时间页或书架。阅读记录新增月历、每日目标与“已闭环”。
  Add selectable clocks and the recent-reading folder, matching time pages, monthly records and daily goals.
- 猫眼与 Kehai 秒针在普通、快刷、水波纹模式使用短局部刷新；Kehai 保留一圈浅色轨迹，避免动态区域被主页水波纹改写为较重刷新。修复书夹预览、边线与返回键反馈。
  Preserve short local clock motion in all main modes, a bounded Kehai trail and exact feedback borders.
- `books` 下的子目录可作为书架分类，从书架题头下拉选择；切页往返保留分类与当前页。书架布局位置及封面显示同步调整。
  Select shelf categories from books subdirectories and retain category/page positions across navigation.
- 设置列表使用跟手黑白滚动并加入可打断的减速惯性；普通与水波纹停止后补回灰阶，快刷保持黑白。滚动复用有界缓存，低内存回退原绘制。
  Add interruptible inertial settings scrolling with bounded binary caches and gray settling where needed.
- 阅读字重改为胶囊减/加，每次微调 1/16 像素覆盖率，旧配置保留实际粗细；自动全刷后的纯文字沿普通文字成像路径，减轻第一页偏淡。
  Refine weight by 1/16-pixel coverage steps, preserve legacy thickness and align automatic text cleanup with ordinary text formation.
- 合入 PR #27、#28、#31 原实现：竖排阅读、翻页器完整报告帧学习与连接订阅修复、RTC 崩溃记录及锁屏图标统一。
  Include the original PR #27/#28/#31 implementations for vertical reading, Bluetooth reports/link reliability, crash records and lock icons.
- 关机菜单采用滑动确认与圆形重启/取消；头像成品缓存到 TF 卡，票根使用系统字体与抗锯齿，首页边线及标题可读性优化。
  Add slide-to-off, round restart/cancel controls, cached avatars and clearer system-font ticket/Home rendering.
- 官网刷机、TF、联网 OTA 与 GitHub Release 来自同一次产品构建，保留历史固定下载地址。
  Publish one product build for USB, TF, OTA and GitHub Releases while retaining immutable older downloads.

## 0.3.3-rc91 / 2026-10-12

- 合入 PR #24、#25，支持 EPUB 内嵌 TTF/OpenType 字体，修复开书与关闭书内字体的栈溢出，减少大型章节索引和多字体缓存占用。
  Include PR #24/#25 for embedded EPUB faces, stack safety and bounded index/glyph memory.
- 阅读新增五档字重和抗锯齿开关；重排字体与阅读设置，中键长按默认返回首页，底部上滑始终打开工具栏。
  Add five stroke weights and optional antialiasing, paired reading controls, default Home on middle hold and bottom-edge Tools.
- 系统设置重新分组，字号并入系统字体，选择字体可确认同步阅读；100–200%保持单行，阅读工具与子面板适配系统字号。
  Regroup Settings, confirm reader-font sync and fit large system sizes in Settings and reader panels.
- 修复边距滑杆、居中章节题头的两行对齐和间距，字体丢失稳定回退；水波纹插图页使用一次灰阶更新。
  Fix margin stops, two-line heading alignment/spacing and missing-face fallback; present illustrated ripple pages once in gray.
- 书架、首页续读与拼贴复用TF成品封面；书架整页准备好后一起显示，修复偶发封面漏刷，保留冷缓存有界准备和低内存回退。
  Reuse prepared TF covers, reveal whole shelf pages and retain retries and bounded cold-cache work.
- 壁纸锁屏缓存最终16级灰阶抖动画布；拼贴优化缓存准备，主动锁屏先清白显示圆锁再生成完整画面，保留画质。
  Cache full-quality wallpaper locks and streamline collage preparation with immediate white/lock feedback.
- 主页和阅读水波纹采用8条带、11,090µs目标节拍，保留完整37相与固定底栏；普通纯文字翻页减轻短暂压黑。
  Match main/reader ripple launch timing while retaining gray phases and navigation; soften transient darkening on ordinary text turns.
- 设置只绘制可见行并复用头像，入口箭头独立于字体；阅读状态栏按分钟更新时间。
  Cull Settings rows, reuse avatar pixels, draw independent chevrons and update the reading clock each minute.
- 修复 WiFi 断联重连和确认遗忘；TF容量使用真实挂载分区并重算过期FAT32提示；票根长统计保持边界。
  Fix WiFi reconnect/forget, partition allocation counts and ticket statistic bounds.
- 合入官方 a2a63a6 的睡眠/关机外设与引脚低功耗处理，保留密码、按键唤醒和阅读恢复；功耗以实机为准。
  Port official low-power peripheral/pin parking, preserving PIN, key wake and reader recovery; power draw requires hardware measurement.
- 在线刷机、TF与OTA使用同一次产品构建，Bin、完整USB包和SHA-256发布至GitHub Release；原固定版本地址保持。
  Publish one verified product build for USB, TF and OTA with immutable older URLs and Release checksums.

## 官网说明书 / 2026-10-10

- 上线 21 章、135 项操作、30 幅界面图解的详细说明书和 69 页 PDF；新增「问说明书」，支持关键词、口语问句与模糊检索，直接定位到操作步骤。保留旧说明书链接及 PDF 下载地址，固件仍为 rc90。
  Publish the illustrated manual and searchable task finder; retain existing manual/PDF links and the rc90 firmware.

## 0.3.3-rc90 / 2026-10-09

- 固件名称统一为 kiikoread；资料卡移除署名与电量，姓名和编辑垂直居中。水波纹主页使用与普通模式相同的灰阶画面。
  Rebrand the firmware as kiikoread, simplify the profile card and center its name/edit action; ripple and normal main pages share gray artwork.
- 新增 kiikoread.com 主站联网升级，失败回退 GitHub；保留原刷机与更新入口。正式二进制统一保存到 GitHub Releases，网页与 OTA 使用经过 SHA-256 验证的同一构建。
  Add primary-site OTA with GitHub fallback, preserve existing flashing/update entry points and store official binaries in Releases with verified website/OTA mirrors.
- 合入 PR #22：缺失系统字体不再每 750ms 反复加载内建字体并触发全刷，字体回到卡内后仍能自动加载。
  Merge PR #22: missing system fonts no longer repeatedly reload the built-in or trigger full refresh; retries still pick up restored files.
- 合入 PR #23：只读确认卡内为不支持的文件系统时提供格式化提示，需再次确认清空全部数据；初始化、供电与读写错误不误提示格式化。
  Merge PR #23: offer formatting for read-only confirmed unsupported media, with explicit second-tap data-loss confirmation; avoid misleading hints for initialization, power and I/O failures.


- 官网品牌改为 kiikoread，采用纯白界面；增加可切换放大的界面展示、功能介绍、统一版本的刷机/下载/日期/日志，以及同源在线和 PDF 使用说明。保留原发布包、历史版本与升级网址。
  Rebrand the bright-white website as kiikoread, with selectable enlarged screen previews, features, consistent release flashing/download/date/notes and shared HTML/PDF documentation. Preserve published binaries, archives and upgrade URLs.
- GitHub 刷机首页跳转至 kiikoread.com，保留版本选择与页面位置；原固件下载、使用说明和联网更新地址继续可用。
  Redirect the GitHub flasher homepage to kiikoread.com with the selected version and page anchor preserved; existing firmware, manual and OTA URLs remain available.

- 应用确认的较小列表封面120×156，保持原中心点、右侧书名/作者/进度及局部弹动，等比例填充并居中裁切。
  Apply the approved 120×156 list covers, preserving their center, text/progress positions and local bounce with aspect fill and centered cropping.

- 列表封面点击改为小幅上弹后回位，复用现有封面，仅刷新移动范围；快刷黑白、普通/水波纹灰阶，无额外反馈框。
  List covers bounce and settle using existing artwork and movement-only local differentials; fast retains BW and ordinary/water grays, without an added frame.

- 密码锁屏缓存增加样式与壁纸身份校验，切换后深睡唤醒/冷启动不再误用之前的拼贴图；自动休眠与手动锁屏继续使用当前选择。旧缓存自动重建，密码和配置不受影响。
  PIN artwork caches now check the selected style and wallpaper identity, preventing stale collage reuse on cold/deep wake. Auto/manual lock keep the current choice; old artwork is regenerated without changing credentials or settings.

- 修复 TF 卡检测通信失败被误判为拔卡的问题；高速初始化失败先等待重试，再降至 20/10 MHz，`ESP_ERR_INVALID_STATE` 不再直接终止重试。初始化错误不自动建议格式化；保留真实拔卡失效、显式重挂及 USB 独占保护。
  TF detection I/O failures no longer masquerade as removal. Failed high-speed initialization settles and retries before 20/10 MHz fallback, including `ESP_ERR_INVALID_STATE`. Init failures do not suggest formatting; true removal, explicit remount and USB ownership guards remain.

- 新增 D 版书架拼贴锁屏：原比例真实封面、15° 倾斜、圆角与16级灰阶；资料卡名字及大小分明的书籍数量使用导入系统字体原生绘制，TF缓存、有界工作内存与低内存回退。
- 移除半透明书袋和书脊样式，旧设置及备份回退亚克力；保留票根、壁纸、两种原书架样式及原睡眠恢复逻辑。
- 浅色拼贴封面增加沿真实圆角边缘的细灰描边，缓存自动重建；深色封面保持原样。
- 新增可选四位数字锁屏密码：开启确认、验证原密码后修改/关闭，真实锁屏模糊背景、导入系统字体、圆内按下反馈及局部黑白快刷；浅睡/深睡/冷启动验证后恢复页面，取消保持锁定。凭据使用加盐摘要，普通配置恢复不覆盖；低内存、触摸读错和多指取消不会绕过验证。
- 密码键盘输入仅局部更新圆内反馈及密码圆点，不处理背景或字形，优化数字、删除与取消的响应速度。
- 新增每页四本的列表书架：统一尺寸的等比裁切封面、作者、阅读进度、收藏标记及双行长标题，支持翻页、重启和配置恢复。
- 拼贴封面新增柔和投影，白色文字描边减细；锁屏图像使用16级灰阶抖动表现256级输入亮度，保持有界工作内存。
- 密码顶部锁图标改为平滑圆角，减轻背景模糊；密码入场一次显示完整画面，不补刷第二段背景，低内存与冷启动直接显示最终密码页。
- 列表书架外框和分隔线加深至双像素，常用按钮、设置卡片边框同步加强，快刷与水波纹模式保持可读。列表封面统一120×156像素，等比放大后居中裁切。
- 首行缩进新增1像素微调（-20～+20，默认0）、数值点击复位及重启/配置备份恢复，正文标题与对齐块保持原位。

Add a D-layout library collage lock with aspect-preserved real covers, native imported system typography, SD caching, bounded workspaces and safe memory fallback. Pale covers gain a fine rounded gray outline. An optional four-digit PIN verifies wake/cold starts, retains the saved page, confirms setup and requires the old PIN for changes. Cached frosted lock art and native system glyphs use local feedback; faults cannot bypass authentication. Salted credentials are excluded from ordinary backups. Retired pocket/spine settings migrate to acrylic.

- 文件页的存储卡卡片在“卡在但文件系统读不了”时改为显示“需要格式化 / 格式化可能可以解决此问题”，点一次询问、再点一次才格式化，点别处取消。此前这种情况与“卡没插”显示同一句话，把用户指向了错误的排查方向。
  The storage card on the files page now reads "needs formatting / formatting may fix this" when a card is present but its filesystem cannot be read, with one tap to ask and a second to format (a tap elsewhere cancels). It used to show the same line as an absent card, which pointed at the wrong problem.
- 加快主页水波纹：使用与阅读相同的一拍一带调度，将24带83拍改为16带52拍，保留12ms目标节拍、完整37相、原灰阶波形、底栏范围与异常恢复。软件目标由996ms缩至624ms，实际速度与残影需实屏确认；阅读翻页、刷新模式默认值不变。
  Speed up main-screen ripple with reader-style compact launches: 16 bands/52 ticks instead of 24 bands/83 ticks, retaining the 12ms target, all 37 phases, gray waveform, navigation bounds and recovery. The software target drops from 996ms to 624ms; hardware speed and ghosting require device checks. Reader turns and refresh-mode defaults are unchanged.

## 0.3.3-rc89

- 移除微信读书登录、在线书架、同步、下载功能与相关源码、测试及网页预览。文件管理直接显示 WiFi 传输、热点传输、USB 传输三个入口，加深传输卡片边框。
- 联网升级前释放闲置封面缓存，低内存时安全提示；屏幕扫描与固件擦写互斥，进度条只追加新增黑段及百分比，避免频繁整页清理。保留有界重试、SHA256 校验、取消等待与双槽回退。
- USB 完整包、TF 升级包和 OTA 清单统一更新，基础包分区与用户数据地址保持兼容。

## 0.3.3-rc88 · 2026-10-08

- 优化启动、EPUB 元数据与封面加载的栈占用；快刷只缓存黑白封面，普通/水波纹只缓存灰阶封面，切模式释放旧格式，低内存时安全回退。
- 异常开书保护：开书被断电或异常重启打断后，停止自动重试该书，进入可操作页面；已记录书名时提供返回首页及确认删除，不自动删除文件。
- 深睡前保存书籍、阅读位置、全屏和主页面状态，唤醒后恢复；恢复记录只消费一次，避免失败后反复尝试。
- 中键长按可自定义，包含返回首页；三键短按或中键长按至少保留一个阅读工具栏入口，旧配置/恢复配置缺少时补为中键短按。配置备份 v11 兼容旧版本。
- 新开书默认全屏；翻页收起工具栏并回到全屏，保留旧布局阅读锚点，避免重排跳读。中间双击独立切换全屏。
- 下拉快捷面板及四个圆形按钮加深框线，三种主页模式统一灰阶；票根统一系统字体，浅睡解锁恢复此前阅读字体。
- 首页继续阅读书名使用系统字体，阅读书名与章节标题使用阅读字体，复用当前字体按原 40/32/48 像素直接生成字形；不增加内建字库或同时加载两份字体。
- 在线刷机、TF 升级 Bin 与联网 OTA 同源发布，保留双槽回退与已有数据；下架 rc87，保留原发布的 rc85/rc86 可选。

Startup and EPUB/cover stack use are reduced, with one bounded cover format per main refresh mode and safe low-memory fallback. Interrupted book opening stops automatic retries and offers Home or confirmed deletion for a recorded book. Deep sleep checkpoints the reader and consumes resume once. Custom middle-hold actions, persistent Tools-entry repair and compatible v11 backups keep controls accessible. Page turns close Tools and restore full screen without skipping the old anchor. Quick controls use clear gray borders in all modes; tickets use the system font. Home titles reuse the active system face at 40px, reader titles the active reading face at 32/48px, without extra embedded faces or concurrent font loads. USB, TF and OTA share a compatible rollback build. rc87 is withdrawn; original rc85/rc86 remain available.

## 0.3.3-rc87 · 2026-10-08 · 已下架 / Withdrawn

- 在线刷机页新增正式版本选择：默认最新版本，也可刷入原发布的 rc86 或 rc85；显示对应发布时间并提供该版 TF 升级 Bin。
  The web flasher offers the latest release plus original rc86/rc85 builds, showing each publication time and matching TF image.
- 主页刷新模式：系统设置可选普通、快刷、水波纹，默认普通。普通及水波纹保留灰阶；快刷使用黑白文字与封面，亚克力书架采用细腻规则的棋盘格点阵。
- 四个主页面切换保留底栏图标与文字，选中小横条单独局部刷新。修复锁屏返回后底栏消失，并限定内容刷新边界；手动全刷、首次进入、解锁和异常恢复保留完整刷新。
- 底栏图标、圆形返回键与书架管理/导入按钮增加点击反馈；修复底栏放大动效周围的矩形白边。三种主页模式统一保留原灰底，按真实变化缩小推屏范围，释放、滑出、多指或切页时复位。
- 新增顶部下滑快捷菜单：从顶部边缘下滑可连接最近保存的 WiFi / 再次断开、切换蓝牙、全刷与锁屏。向上滑、点击外部或返回键收起；传输忙碌时保留网络，蓝牙沿用现有内存保护。
- 阅读按键可自定义，提供两套推荐：设置1为左上一页 / 中阅读工具栏 / 右下一页；设置2为左首页 / 中切换全屏 / 右阅读工具栏。中键长按固定全刷，中间双击切换全屏不随设置变化；单击左右各半或上1/3、下2/3翻页。配置随重启及保存恢复保留。
- 阅读水波纹替换为快速动画；灰阶相位保持完整，插图与手动/周期全刷仍使用原路径。优化主页面水波纹条带与快刷退出书架的同向擦白；缓存最多九张黑白封面，切页与锁屏不回放旧画面。
- 改善阅读题头与首页书名中补充汉字放大的锯齿，保持原字宽与排版。
- 在线刷机、TF 升级 Bin 和联网 OTA 来自同一次产品配置编译；核对双槽基础包、升级校验值和版本，保留已有配置、阅读数据与 TF 卡内容。联网更新确认页显示简短日志。

Main screens offer ordinary, fast and water refresh modes. Ordinary/water retain grays; fast uses monochrome artwork and fine checkerboard acrylic. Retained navigation has local marker and press feedback, with actual-change bounds protecting its background and labels. A top-edge quick layer controls saved WiFi, Bluetooth, refresh and lock. Reader keys are configurable with two presets, fixed middle-hold refresh and independent center double-tap full screen. Fast reader ripple, bounded cover caching and interpolated supplemental Han glyphs improve the display. USB, TF and OTA share one product build with compatible slots, rollback and release notes.

## 0.3.3-rc86 · 2026-10-08

- 新输入法：九宫格只输入中文拼音；全键盘支持中英文与大小写切换，数字和符号使用独立页面。文件/文件夹改名、资料卡名称、状态栏签名、书架搜索、书名编辑及 WiFi 密码共用这套界面。
- 连续拼音支持 488 个离线常用词组与选词后联想；同音字支持跨批次翻页，九宫格「更多」可选择其他读音。拼音未确认时保留输入，确认后再保存；内建黑体补齐 GB2312 的 6763 个汉字。
- 补齐常亮输入光标：轻点文字或左右箭头调整位置，在光标处插入或删除；长内容跟随光标滚动，完整处理 UTF-8 字符、密码显示/隐藏及大系统字号。
- 输入按键按下加深并轻微下沉，松开或移开恢复，只刷新该键。删除键长按约半秒后连续删除，先删待选拼音，再删光标前的文字，松手、移开、多指或离页立即停止。
- 优化打字刷新：连续输入使用局部快刷；联想词栏累计变化六次后，停手约 700 毫秒只做一次局部灰阶整理，减轻残影。打字和长按删除期间延后整理，静止画面不反复刷新。
- WiFi / 热点传书网页新增状态栏签名设置，可保存或清空，重启后保留。
- 修复部分 EPUB 兼容性，避免局部对齐样式影响整篇正文；采用 PR9 图文混排核心，补齐图片缓存与异常回退，优化图片缩放。网页刷机增加设备筛选和中文连接提示，保留擦除确认。
- 在线刷机、TF 升级 Bin 与联网 OTA 同源发布，更新日志同步显示在设备更新确认页；继续保留双槽回退、已有配置与阅读数据。

The shared keyboard now supports Pinyin-only T9, bilingual QWERTY, case switching and separate number/symbol panels. Continuous Pinyin uses 488 offline phrases, continuations and pageable homophones; the embedded black face covers 6,763 GB2312 Han characters. Steady carets, insertion-point editing, local key feedback and held backspace are available across native input scenes. Typing stays on fast local updates; six candidate-strip changes followed by a 700 ms touch-free pause trigger one bounded gray settle. AP/STA web transfer can save or clear the persistent status signature. EPUB alignment, image resampling and PR9 image flow are improved. USB, TF and OTA share one build, with release notes, rollback and user-data preservation.

## 2026-10-07 · mindreset

- 合并 PR12：封面和插图采用中心对齐的双线性缩放，改善细线与图中文字的显示；修正映射运算及损坏 PNG 的缓冲区释放。
- 合并 PR13：网页刷机默认保留用户数据，擦除需主动勾选；仅列出 Espressif USB 串口，安装对话框改为中文，并避免翻译触发反复处理。保留当前 rc85 固件包与升级版本。

PR12 adopts center-aligned bilinear image resampling with safe coordinate arithmetic and PNG cleanup. PR13 defaults to preserving user data, filters Espressif USB ports and translates installation dialogs without repeated mutation handling. Published rc85 binaries remain unchanged.

## 0.3.3-rc85 · 2026-10-07

- 优化阅读文字翻页：纯文字页之间使用 CrossMux / FreeInk 文字波形与灰阶差分，减少重复擦白及新字等待；普通阅读与全屏均生效。涉及插图或图文混排时保留原灰阶路径，水波纹、周期及手动全刷继续有效。在“阅读设置 → 翻页效果”选择“默认效果”体验。
- 图文兼容修复：补齐 SVG 包装图片及较长 JPEG 头部的尺寸识别，缺失插图记录时继续绘制正文；保留章节分页、标点规则、阅读线和未知图片回退。
- 蓝牙扫描隐藏没有广播名称的设备；后续扫描响应补齐名称后可显示，点击对应正确设备。
- 翻页器适配：扩展原生按键支持，保留手动映射优先及键盘修饰键保护。
- 在线刷机、TF 升级 Bin 与联网 OTA 同源发布，更新日志同步显示在 rc84 及以后固件的更新确认页；保留兼容双槽布局与用户数据。

Text-only turns use the CrossMux / FreeInk waveform and grayscale differentials in normal and full-screen reading. Image/mixed-page transitions, water effects and periodic/manual cleanup retain their existing paths. This release also includes SVG/JPEG image-flow fixes, named-device Bluetooth filtering with later name updates, and Bluetooth pager compatibility with manual-mapping precedence. USB, TF and OTA share one build, preserving the dual-slot layout and user data. Devices running rc84 or later display the release notes before online installation.

## 0.3.3-rc84 · 2026-10-07

- 图文混排：小图与正文在同一页排版，按比例缩放，多图接排。
- 滑动翻页：默认支持左右滑动，兼容上下翻页模式。
- 自动休眠：可选 1 分钟、5 分钟、10 分钟或关闭。
- 蓝牙翻页器优化：学习映射显示识别到的实际按键。
- 图标 UI 优化：升级、BOOT 和阅读设置使用独立图标。
- 文件管理排版优化：统一操作和分页按钮，增强文字与线框可读性。

联网更新确认页新增简短更新日志，提供“稍后 / 开始更新”。从 rc84 开始，后续更新可在设备上查看日志；旧固件升级本次版本前可在在线刷机页查看。完整刷机、TF 升级和联网 OTA 使用同一编译，保留兼容的双槽基础包、现有设置与阅读数据。

The release adds inline images, swipe page turns, configurable idle lock, clearer learned Bluetooth keys, distinct UI icons and improved file-management layouts. From rc84 onward, the online update offer displays short release notes before installation. USB, TF-card and online updates share one build and retain the compatible dual-slot layout and user data.

## 2026-10-07 · Codex

- rc83 本地测试：删除“全局刷新模式”测试项及实验刷新引擎，旧 NVS / TF 备份不再开启该模式，保留标准刷新及阅读全刷设置。晃动翻页改用新加速度采样区分左右：左晃上一页、右晃下一页；一次横向动作触发，过滤倾斜、其他方向及回弹，触摸和工具栏期间禁用，离页恢复传感器睡眠。方向与灵敏度已通过模拟轨迹回归，仍需实机试用。

- rc83 本地测试：采纳 PR #7 的比例缩放、页内多图及后续正文接排；保留有界尺寸探测，补齐未知尺寸回退与每张图失败提示，按布局代次更新缓存，章节加载失败保留原章及进度。混排页保持灰阶并沿用普通翻页刷新，纯图页保留完整灰阶刷新。采纳 PR #8 的蓝牙学习按键值显示；已覆盖多图、空间边界、重排、隐藏图片、内存失败与按键结果回归，真实图片观感及翻页器待实机验证。

- rc83 本地测试：合并 PR #5/#6，USB 停止失败时保留存储卡所有权并允许重试，恢复串口 PHY；补齐 CI 的 USB 配置。管理书架移除旧菜单入口，修复“下一页”误进示例菜单，统一筛选、操作和分页按钮，保留跨页勾选。EPUB 同文件内的正文章节从新页开始，标题与开头正文同页；过滤目录链接、简介及版权信息，普通小节继续接排。本地版本号保持 rc83，固件尚未发布到在线刷机页。
## 2026-10-07 · mindreset

- 蓝牙翻页器的按键映射改为显示实际学到的键：以前只写“自定义按键”，现在直接给出该键的原始字节（如 0x88），字节下标或报告号非零时一并补上，两个绑定不会再看起来一样。
## 2026-10-06 · mindreset

- 正文图片改为图文混排：插图不再占据整页，而是作为页面流里的一个块放在正文之间，图下方接着排后续正文；只有当前页放不下时才翻页。图片按栏宽等比缩放、不超过一页高、不放大，水平居中。
- 排版仍然不依赖图片解码器：尺寸由调用方通过回调提供并在本章缓存；优先使用已缓存的有界头部尺寸，无缓存及回调时保留整页显示。

## 2026-10-07 · mindreset

- 蓝牙扫描列表不再列出没有广播名的设备：那些行只能显示地址，认不出是什么设备，地址还会把信号强度挤掉。
- 翻页器适配：新增按键协议的原生支持：0x01 上一页、0x02 下一页，无需学习。

## 2026-10-06 · Codex

- 固件 0.3.3-rc83：
  - 书架封面抽出与取消改用局部灰阶差分，只驱动变化的像素，减少点击时的黑闪。
  - 设置主页、蓝牙及扫描页滑动更早响应，一次至少移动三行；各设置列表仅刷新内容区，取消滑动时按次数强制黑白清屏，到边界不重复刷新。
  - 状态栏时间、电量百分比增至 28 像素，个性签名增至 26 像素；按实际字形与图标中心对齐，长签名避开两侧信息。
  - 修复蓝牙翻页器及扫描页面叠出设置资料卡。
  - 修复文件管理首次打开图片时重启；支持直接打开根目录或其他目录中的图片，并浏览同目录图片，保留灰阶层次。
  - WiFi 与热点传书网页新增“添加文件到当前目录”，支持根目录、多级目录、任意格式与空文件；保留原名称，同名覆盖需确认，失败保留原文件。
  - 系统字号上限提高到 200%，随重启及配置备份保留；合并屏幕 PMIC 的 VCOM 门控更新，重新开启 VCOM 后等待 2 毫秒再扫描。
  - 完整刷机包、TF 升级 Bin 和联网 OTA 使用同一固件与版本；基础包保留双槽升级、首次启动确认及异常回退能力。已通过产品配置编译和宿主回归，实际刷新观感与硬件门控效果仍需实机验证。

- 固件 0.3.3-rc82：联网检查发现新版本后直接显示“稍后 / 开始更新”，修复任务收尾后按钮仍显示“取消联网更新”的问题；下载进度至少间隔 5 秒且变化达到 5% 才更新，仅刷新百分比与进度条区域，不累计整页清残影。更新期间关闭 WiFi 省电并在结束后恢复；连接等待延长，短暂断网允许最多三次重连续传，核对续传范围、文件长度与完整 SHA256，失败保留原版本。阅读页返回键、书名与收藏图标统一视觉中心线，内建字体按实际字形边界居中。基础包、TF 升级 Bin 和联网清单同源发布，保留双槽回退及已有数据；已发布的版本下载地址继续保留，避免旧清单缓存导致更新文件失效。已通过产品配置编译、宿主回归和原生渲染核对，真实无线环境与墨水屏观感待验收。
- 在线刷机页在当前版本下显示更新时间，统一使用北京时间；后续打包自动更新记录。
- 固件 0.3.3-rc81（联网测试版）：修复 WiFi/热点选择页按下反馈误触发整页刷新、取消点击而无法进入传输页的问题；已保存 WiFi 时直接启动传输，未配网时进入网络选择。状态栏 WiFi、蓝牙图标使用与电池相同的黑色，稍微缩小并靠近电量百分比。所有圆形返回键外圈统一加深。
- 书架按下书籍向上抽出，取消触摸恢复；页码两侧新增对齐的上一页/下一页箭头，首末页相应箭头置灰。蓝牙翻页器上/左键上一页，下/右键下一页；支持已学习的原始按键，同一报告只执行一次。书架翻页复用局部刷新，各书架样式均生效。基础包与升级包仍使用兼容双槽分区，提供官网 HTTPS 联网更新与 TF 升级 Bin；实机联网升级及观感待用户验收。
- WiFi 连接页采用加深双层卡片边框、清晰分隔线和较大字号；已连接时提供断开按钮，断开保留凭据，传输中拒绝断开。阅读设置新增翻页模式：上下模式以正文高度三等分，上区上一页、中区切换全屏、下区下一页；普通及全屏阅读均生效，支持上下滑动翻页。左右模式保持原有规则，偏好随重启及配置备份恢复。
- 固件 0.3.3-rc80（本地验证版）：蓝牙翻页器移到 WiFi 下一行，状态栏加入 Lucide 蓝牙图标并支持与 WiFi 同时显示；按键学习主动响应，顶部显示「识别成功」，清理旧输入并修正带报告 ID 的键盘释放识别。网络分配前串行暂停蓝牙，防止同时初始化；保留内存门限，检查蓝牙任务创建结果，失败回收资源，网络退出后恢复蓝牙。修正 BOOT 行点击范围。
- 阅读「刷新设置」改为「阅读设置」，集中放置全屏沉浸、电源键翻页、关闭书内图片及长按圆圈键全刷；删除系统设置「阅读操作」。长按圆圈键默认回首页，开关开启后整屏全刷，并加入配置备份。普通阅读题头增加圆形返回键，返回进入书籍前的页面。首行缩进按实际字体字宽计算，并校正段首引号、括号的可见边缘。
- 固件 0.3.3-rc79（本地验证版）：合并 PR #3 的渐进式 JPEG 封面及比例裁剪、PR #4 的 BLE 蓝牙翻页器和大缓冲 PSRAM 分配。蓝牙设置、扫描及按键学习页按当前设计统一，修正滑动、返回与停止扫描；WiFi/微读/OTA 工作时暂停蓝牙以减少内部内存竞争。
- 设置新增“升级和恢复”分组：系统升级、保存与恢复、BOOT 刷机独立成行；文件管理移除 BOOT 按钮。系统升级支持已有 TF 卡升级和 HTTPS 联网 OTA：验证官方源、设备型号、版本、应用槽、完整大小、SHA256 与镜像，只写空闲槽，完整验证后才切换启动。官网发布工具同时生成全量基础包、TF Bin 与 OTA 清单，发布前验证基础包能力与分区兼容性；本地页面增加 Bin 下载，尚未发布新版在线固件。
- 微读同域完整响应复用 HTTPS 会话，清除旧请求鉴权头与回调；延长建连与无数据等待，临时断网允许重连和有界重试，避免逐章重复握手。真实账号下载和 BLE 设备配对仍需实机验证。
- 电量显示统一采用 PMU 软件油量表，优先读取带 CRC 与有效位的轻量快照，无有效读数显示未知，资料卡和状态栏使用相同取整。此修订不改变 PMU 电压或保护参数，实际电量误差仍需充放电记录验证。新增蓝牙开关加入配置备份，兼容旧备份。

- 固件 0.3.3-rc78（本地验证版）：微读返回键明确返回文件管理，书籍详情进入独立二级页，不再显示书架同步与退出登录；下载文件使用微信书架书名，不再追加书号。封面始终下载，与正文插图开关分离；封面获取失败会提示重试。微信书架增加跨页多选、全选本页及逐本批量下载，一次最多选择 1024 本，取消或离页会停止队列，已完成文件保留。保留 rc77 的目录字体、可读性与图片空白占位修复。已通过本机编译及主机回归，真实账号下载与实机操作仍待验证，未上传在线刷机页。
- 固件 0.3.3-rc77（本地验证版）：阅读目录全部使用当前系统字体，返回正文恢复独立的阅读字体；加深目录卡片及按钮边框，增大上页、下页、快翻十页和百分比跳转文字。排版跳过插图前后的空白段落、全角及零宽占位，减少多余空行和空白页，关闭图片时正文正常接续，保留原文定位。保留 rc76 的传书与微读功能；未上传在线刷机页。
- 固件 0.3.3-rc75（本地验证版）：补齐已确认的首页排版，题头与书架使用相同位置、字号和绘制方式，横线统一在同一高度；继续阅读区整体下移 40 像素，点击范围与封面局部刷新同步调整。近七天图表保持原布局。
- 固件 0.3.3-rc74（本地验证版）：采用 PR #2，从文件管理打开并保存进度的 TXT/EPUB 自动加入书架；管理页新增「移出书架」，保留文件及阅读进度，再次阅读自动上架。批量按钮改为两行三列；移出状态随 TF 卡配置备份恢复，兼容旧备份。保留 rc73 的 Lucide 图标。已通过产品配置编译与主机回归测试；实机显示和操作待验收。

## 2026-10-05 · DeepSeek Harness

- 统一采用 Lucide 图标：底栏、WiFi、返回键、阅读工具栏、设置、文件管理与电源弹窗使用同一套线条样式；已添加书签显示轮廓加勾。同步图标生成工具与许可声明。
- 书架收录读过的书：从文件管理打开并保存进度的 TXT/EPUB 可以位于 TF 卡任意位置，返回书架时自动补入；已删除或文件大小已变的记录不补入。管理页新增「移出书架」，只隐藏条目，文件和进度保留，再次打开重新上架；批量按钮改为两行三列。

## 2026-10-05 · Codex

- 固件 0.3.3-rc73（本地验证版）：完整采用 PR #1 的 Lucide 图标方案，尺寸、线宽和灰度与确认预览一致。已通过产品配置编译与主机测试；实机显示待验收。
- 固件 0.3.3-rc72：修复部分 EPUB 兼容性。
- 固件 0.3.3-rc71（本地验证版）：关闭书内图片时，排版跳过插图块并让前后正文接续，不再保留插图页或显示「此插图暂无法显示」占位提示；纯插图章节仍沿用跳过逻辑。尚待实机验证，未上传在线刷机页。
- 固件 0.3.3-rc70（本地验证版）：全屏阅读底部细进度条上移 4 像素，并同步调整独立刷新区域，避免翻页时正文刷新影响进度线。尚待实机视觉验收，未上传在线刷机页。
- 固件 0.3.3-rc69：修复票根锁屏遇到持续按键中断时可能停在静态画面的问题；浅睡期间定时补查电源键，睡眠连续失败时返回可操作页面。票根仅使用已有封面与元数据缓存，避免锁屏时解析大 EPUB；书架多级目录扫描改为固定栈占用，文件管理递归目录操作的路径与复制缓冲移到外部内存，降低重启风险。配置恢复先校验再替换旧阅读记录，并核对写回的 WiFi 凭据。阅读「刷新设置」增大文字并加深选项边框，字体列表长名称按卡片宽度截断。已通过产品配置构建和主机回归测试；仍待实机验证长时间锁屏、书架进入与配置恢复。详细说明见 [rc69 更新说明](development/history/releases/RELEASE_NOTES_0.3.3-rc69.md)。
- 固件 0.3.3-rc68：将「封面与书脊」样式标为测试版、非正式版本；在线刷机固件汇总 rc59–rc68 的双槽升级、书架收藏、排版、壁纸、备份及 WiFi 内存优化。详细功能、验证范围与注意事项见 [rc68 更新说明](development/history/releases/RELEASE_NOTES_0.3.3-rc68.md)。
- 固件 0.3.3-rc67（本地验证版）：书架封面和书脊收藏标记统一为黑色，深色背景增加贴合图标轮廓的白边。优化 WiFi 在线时打开图书的内存余量：开书前释放可重建的封面与预渲染缓存、按需启动预渲染，并清理损坏 EPUB 索引的临时分配；书架打开失败会显示原因。尚待实机联网上书验收，未上传在线刷机页。
- 固件 0.3.3-rc66（本地验证版）：TF 卡壁纸按原比例居中裁切铺满锁屏，横图和方图也能在解码器限制内显示；配置备份增加已保存的 WiFi 名称与密码，兼容旧备份，并提示妥善保管 TF 卡。设置页卡片边框与分隔线加深、加粗；所有书架样式都将收藏书排在前面，再按当前排序规则排列。含 rc65 的收藏标记白底修复。尚待实机视觉验收，未上传在线刷机页。
- 固件 0.3.3-rc65（本地验证版）：书架封面和书脊左上角的收藏标记去除纯白矩形底块，按封面局部灰度使用深色或浅色实心标记；阅读页标题旁的收藏标记保持原有深色样式。尚待实机视觉验收，未上传在线刷机页。
- 固件 0.3.3-rc64（本地验证版）：保存书签后图标完整填黑；配置备份新增每本书的阅读进度、阅读时长、书签、收藏和自定义书名，继续保存设备资料与状态栏签名，并兼容旧备份。阅读首行缩进不再叠加书籍原文自带的段首空格；修复书架管理页返回键、筛选点击区域与未选书时的操作提示。尚待实机视觉及 TF 卡恢复验收，未上传在线刷机页。
- 固件 0.3.3-rc63（本地验证版）：新设备的阅读行距默认改为滑块中档 130%，保留已有手动设置；普通阅读正文上缘适当下移、下缘延伸到页脚进度区前，全屏正文上缘增加留白、底部细线移近屏幕下沿，正文与刷新范围同步调整，减少页面下方留空。尚待实机视觉验收，未上传在线刷机页。
- 固件 0.3.3-rc62（本地验证版）：资料卡头像与圆角遮罩使用同一边界，并自动裁去明显的白色留边；阅读工具栏的书签在保存后显示可辨认的选中态，收藏图标的 V 形尾部完整填充，图标线宽与深浅统一。尚待实机视觉验收，未上传在线刷机页。
- 固件 0.3.3-rc61（本地验证版）：调整热点与同 WiFi 传书服务的启动顺序和内存占用，修复 `ESP_ERR_HTTPD_TASK` 导致网页无法启动；减少上传时的碎片写入、关闭传书期间的 WiFi 省电，并延长弱信号重连窗口。书架支持扫描 books 内的多级目录、收藏书籍和书脊样式；统一阅读字号与最近阅读排序，增加关闭书内图片选项。改善插画刷新、壁纸填充与灰阶、正文两侧对齐和全屏排版；修复设备名称与状态栏签名保存失败时无提示的问题，并对齐设置资料卡中的电量与编辑文字。尚待实机验收，未上传在线刷机页。

## 2026-10-04 · Codex

- 固件 0.3.3-rc60（本地 OTA 验证版）：作为 TF 卡升级包验证双槽切换、首次启动确认和异常自动回退；功能与 rc59 相同，版本号递增用于确认设备内升级成功。
- 固件 0.3.3-rc59（本地 OTA 基础版）：16 MB Flash 改为双应用分区，保留原 NVS 与内部存储地址；文件管理新增 TF 卡本地升级，可校验并安装根目录的 `Pico-update.bin`。升级只写空闲槽，完整性校验通过后才切换；新槽首次启动失败会自动回退。首次启用仍需通过电脑完整刷入本基础版。
- 固件 0.3.3-rc58：修复部分 EPUB 打开时设备自动重启的兼容问题；目录解析的大块临时数据改用外部内存。实机已验证可正常打开并翻页。同步发布 rc51–rc57 的目录、阅读、界面、锁屏及配置备份改进。
- 固件 0.3.3-rc57（仅本地构建）：设置主页恢复原行高并支持上下滑动，固定状态栏、标题和底栏；资料卡头像居中裁切到圆角遮罩，状态栏签名按屏幕中心对齐，输入法显示中文、英文小写和英文大写切换状态。首页模块边框、分隔线和书架横线加深；无封面书籍的第三种模板换成上深下浅的新样式并重建旧缓存。新增的设备资料、显示、睡眠和阅读选项随 TF 卡配置备份与恢复。
- 固件 0.3.3-rc56（仅本地构建）：壁纸与票根锁屏均在浅睡十分钟后转深睡；电源菜单可选彻底关机或先浅后深。修复全屏阅读底部进度线；设置新增首页强刷、设备名称与头像、状态栏签名；BOOT 入口移到文件管理。
- 固件 0.3.3-rc55（仅本地构建）：首页、书架、设置与文件管理切换改用完整 16 级灰阶表，每 3 次切换做一次同屏灰阶整理，保留每 12 次完整清残影；文件管理的卡片边框、列表分隔线与辅助文字加深、加粗或放大。阅读线样式与上下位置移入独立二级菜单；全屏翻页将最底部进度线单独差分，减少反复闪动。补齐 rc54 阅读线位置调节的固件实现。
- 固件 0.3.3-rc54（仅本地构建）：阅读线加深至双像素，可在字体设置中上下微调位置并随配置备份；全屏阅读扩展正文区域，屏幕底部增加细的全书进度线；字体设置同级卡片统一高度。刷新设置新增每 30 页全刷与「不强制全刷」，保留手动整屏全刷；旧配置仍可恢复。
- 固件 0.3.3-rc53（仅本地构建）：兼容部分 EPUB 对普通 ZIP 条目设置的扩展标志，修复文件被误判为不支持、无法打开的问题；仍核对压缩方式、文件结构和 CRC，并完成正文读取验证。
- 固件 0.3.3-rc52（仅本地构建）：TXT 和缺少有效封面的 EPUB 自动生成三种确定性灰阶封面，书架、首页与阅读票根共用同一封面缓存；书内有效封面继续优先显示。延续 rc51 的目录快翻、百分比跳转和 EPUB 目录识别修复。
- 固件 0.3.3-rc51（仅本地构建）：目录每次可快翻 10 页，新增百分比跳转；目录标题使用更清晰的固定字号。EPUB 的 NCX 目录缺项时补入 NAV 中可用的章节与同文件锚点，重复项保留 NCX 名称，并重建旧目录缓存。修复阅读排版在默认字间距下调节边距时变化不明显的问题。
- 固件 0.3.3-rc50：阅读正文插图直接使用解码后的灰阶，不再叠加深色增益与系统对比度；翻到插图页时用完整灰阶波形从白底呈现，保留更多暗部层次。系统对比度的首次默认值改为最低档 100%，已有手动保存的设置继续保留。
- 固件 0.3.3-rc49（并入 rc50）：图片页改用独立的完整灰阶波形，先清除上一画面，再从白底呈现原始灰阶，改善插画与照片的层次。阅读统计中的书签支持多选删除；文件管理长按文件或文件夹可重命名、复制、移动或删除，轻点保持打开；书架“管理”与“导入”入口改为当前设计风格。
- 固件 0.3.3-rc48（并入 rc50）：阅读正文按整字列居中，改善右侧留白明显更宽的问题；排版设置新增首行缩进 0／1／2／3 字，默认 2 字，并纳入 TF 卡配置备份与恢复。中文右标点与前字同排，避免标点单独出现在下一行开头或悬挂到右侧留白。

## 2026-10-03 · Codex

- 固件 0.3.3-rc47：修复全屏阅读时水波纹翻页误回退为默认效果；退出全屏后整页重绘章节与进度信息。系统设置新增 TF 卡配置备份与恢复，涵盖字体、排版、显示和阅读操作；恢复前校验文件并对缺失的外部资源安全回退。EPUB 清单与章节按实际数量分配内存，扩展到 8,192 项，并优化大书目录索引与打开速度；修复 1,546 章 EPUB 被旧的 1,024 章上限拒绝的问题。
- 固件 0.3.3-rc46：阅读线改为按字号与行距定位在相邻两行文字之间，并加深虚线和点线，避免线条贴近下方文字；网页刷机页自动读取刷机包版本，修复页面仍显示 rc42 的问题。
- 固件 0.3.3-rc45：修复首次无 TF 卡开机后插卡不会重新探测的问题；挂载完成时首页、书架和文件管理重新读取目录。拔出已挂载的卡仍须安全关闭旧文件并显式重挂，避免误写。
- 固件 0.3.3-rc44（预览构建，未刷机）：字体设置主面板只保留字号、阅读字体、晃动翻页和阅读线；边距、行距、段距和字间距集中到「排版设置」二级面板，保留原有数值与拖动行为。
- 固件 0.3.3-rc43（预览构建，未刷机）：阅读正文轻点中央切换全屏并重新排版；系统设置新增“阅读操作”，可选电源键短按下一页、长按锁屏及全屏隐藏状态栏。字体设置按参考图重排，正文默认首行缩进两字，字间距默认居中并可向两侧微调；新增段距和点击轮换的无／虚线／点线阅读线。
- 固件 0.3.3-rc42：设置页点击 Pico 设备卡片可进入固件升级页，选择“进入 BOOT 模式”后保存 TF 卡写入并重启到 ROM 下载模式；USB 读卡结束时重新将内部 USB PHY 交给串口/JTAG，供电脑再次识别和刷机。网页刷机指南补充 BOOT 模式入口与使用时机。
- 固件 0.3.3-rc41：阅读页底部只显示章节名，移除自动补上的章号；首页续读作者右侧不再显示章号。续读书名优先按当前字体实际宽度缩小字号完整显示，保持原布局位置。
- 固件 0.3.3-rc40：内建系统字体改为稍厚的思源黑体 Medium（500 字重），保持与阅读字体分离；锁屏壁纸选图增加整图预览、上下滑动换图和设定确认；书架样式预览改用无书名的通用封面；TF 卡挂载后按需建立 books、fonts、pictures；目录跳转后立即更新底部章节与百分比；EPUB 大图改用更省内存的渐进缩放解码，改善扉页插图显示。
- 固件 0.3.3-rc39：EPUB 首次打开目录时检查正文中的“第一章 / 第1章”等独立标题；识别到完整章节序列时优先据此生成目录并精确跳转，识别不完整时只校正对应标题并保留书内目录。过滤普通段落提及的章节字样，结果写入索引，后续直接读取。
- 固件 0.3.3-rc38：参照 CrossMux 将 EPUB 正文 spine 与 NCX/NAV 目录分开，保留同一 XHTML 中多个章节的标题与锚点；点击目录跳转到对应正文位置，目录当前项按条目高亮。旧 EPUB 目录索引自动重建；书前作者、出版社资料仍从目录中排除。
- 固件 0.3.3-rc37：首页继续阅读在最近打开的书籍变化后立即更新，复用封面缓存而不重复探测 TF 卡；从首页打开书籍直接显示阅读页，长按中键退出时返回首页。EPUB 目录优先采用书内导航，缺少导航时过滤书前作者、出版社、版权等资料页，阅读进度仍沿用原章节位置。
- 固件 0.3.3-rc36：阅读页刷新设置新增可持久化的「默认效果 / 水波纹效果」，升级后仍默认沿用原刷新规则。水波纹采用 16 条带、37 相错相 GL16，单次差分计算，沿逻辑翻页方向执行；周期全刷仍覆盖整屏。动画异常时重建前后帧基准，缺少相位或低内存时回退普通 GL16。原先移除方向动画的产品决定改为「默认关闭、用户可选」，阅读字体设置面板保持原样。
- 固件 0.3.3-rc35：放大状态栏时间、WiFi、电量百分比与电池图标，并统一垂直对齐；文件管理根目录的文件夹名称与图标居中对齐。
- 固件 0.3.3-rc34：实机验证发现网页上传书籍后写入阅读进度时会触发重启；传输服务任务栈改回内部内存，并在同 WiFi 模式下继续验证上传与目录操作。
- 固件 0.3.3-rc33：热点与同 WiFi 传书共用新版 TF 卡文件管理网页；新增手机锁屏壁纸裁剪，支持拖动位置与缩放，按 Pico 684 × 1216 像素导出 JPG、上传到 pictures 后自动设为壁纸锁屏。目录重命名后同步当前传书目录，防止本次会话的后续上传写向旧路径。

## 2026-10-02 · Codex

- 固件 0.3.3-rc32：热点传书网页改为 TF 卡目录浏览与管理，可逐层查看根目录、分页、新建文件夹、重命名、复制和删除文件或文件夹；书籍、字体、图片分别上传到 books、fonts、pictures。移除网页的图片对比等多余入口，热点限制单设备接入，传输时暂停设备页面交互与定时刷新。文件改名或删除时同步维护阅读进度、字体、壁纸和自选目录设置。
- 固件 0.3.3-rc31：USB 读卡页重排连接状态、存放目录与安全弹出步骤；修复连续写入时缓冲区覆盖和扇区偏移丢失，写卡失败会向电脑返回错误，USB 模式使用更保守的 TF 时钟。
- 固件 0.3.3-rc30：阅读设置刷新图标换成提供的图样，与其他工具图标统一中心和灰度；阅读页放大书名、章节及进度文字；状态栏文字固定字号并与 WiFi、电池对齐；首页统一细节左边距、七日标题基线并加深图表分隔线。
- 固件 0.3.3-rc29：首页按布局编辑器保存的画面重排，移除继续阅读白色卡片与深色按钮底框；封面、书名、作者、章节、进度和七日阅读标题对齐新坐标，柱形图继续显示真实阅读记录。
- 固件 0.3.3-rc28：首页改为继续阅读卡片与近七天阅读时长圆角柱形图，读取已有的每日阅读记录；首页只加载当前阅读封面，避免多本封面逐一刷新。状态栏的时间、WiFi、电量百分比和电池统一垂直中心线。
- 固件 0.3.3-rc27：WiFi/热点传书启动前释放字形缓存；成功的 WiFi 配置请求正常返回，重复文件等请求错误不再把无线服务显示为故障。上传中禁止切页打断文件。USB 读卡禁用主机弹出时的自动二次挂载，先断开主机并排空写入再退出；TF 卡卸载失败不再交给电脑接管。USB 传输缓冲区由 512 字节增至 8192 字节，入口先显示准备页。
- 固件 0.3.3-rc26：阅读目录改由独立页面模块绘制和处理点击，不再复用书架列表与旧局部翻页逻辑。目录使用新版浅灰底与白色圆角章节卡片；章节标题合并换行和异常空白、限制为单行并在超宽时截断，每页 7 节。增加显式翻页入口和章节选中态，目录长按中键不会进入旧示例菜单。
- 固件 0.3.3-rc25：阅读设置的刷新图标按预览改为清晰的粗线回环；阅读页底部进度条与章节、页码信息铺满内容宽度。系统默认使用内建思源黑体，移除“默认无衬线”名称；阅读和系统字体选择后返回设置页仍显示中文字体名称。首页题头改为“首页”，坐标与字号对齐书架页。
- 固件 0.3.3-rc24：阅读设置底栏第四项新增刷新设置，可手动整屏全刷，或选择每阅读 5、10、15 页整屏清残影一次，默认 15 页。文件管理首页直接提供 WiFi 传书、热点传书和 USB 读卡入口；书架「导入」进入文件管理，传输结束后返回文件管理并省去额外白屏。
- 固件 0.3.3-rc23：阅读翻页和周期清残影仅更新正文与底部进度区，顶部状态栏和书名保持稳定；进度条下方的章节信息从 15 像素增至 20 像素，并独立保留右侧页码与全书进度。
- 固件 0.3.3-rc22：联网后对时直接复用已连接的 WiFi，并在写入设备时钟后核对结果；配网列表和遗忘确认页的底栏恢复切换功能。首次刷入本版时将亚克力书架设为显示样式，此后保留手动选择。移除阅读翻页动画及其设置，字体设置恢复紧凑面板，普通翻页继续使用单次灰阶刷新。阅读目录改为独立单行卡片，标题测量与绘制使用相同字体以避免重叠；EPUB 3 目录兼容更多标记格式，目录缺失的正文标题在关闭书籍时写回索引。
- 固件 0.3.3-rc21：设置页和联网状态栏改用 UX v17 预览图同源的 WiFi 灰阶像素图形，保留圆润的双段弧线与水滴形底部；状态栏的图标位置和电池比例同步对齐参考稿。
- 固件 0.3.3-rc20：优化大 EPUB 的 ZIP 目录与流式解压，单个无关大资源不再阻止整本书打开；目录缺项时按需读取章节正文标题。阅读排版支持书内外部 CSS 的有界子集，修正中文左标点折行后的对齐宽度；插图识别补充惰性加载、SVG 图片标签与单图 SVG 包装，并解码图片路径中的实体。
- 固件 0.3.3-rc19：移除微信读书入口和在线阅读模块；底栏第四项改为“设置”，点击后直接进入设置页。同时移除尚未实现的蓝牙设置与 Calibre 传输入口。首次启动新版固件时清除旧版保存的微信读书登录凭据，保留本地书籍。

## 2026-10-01 · Codex

- 固件 0.3.3-rc18：撤销整页 8 相短 DU；系统页面切换改为 30 相 8 灰阶差分 GL16，每 12 次同步做一次完整 GC16 清残影，降低残影且不做延迟二次刷新。

- 固件 0.3.3-rc17：文件首页改为真实 TF 根目录卡片列表并直接提供文件操作；系统页面切换改为 8 相差分 DU，累计 20 次同步做一次 GC16 清残影，取消每次导航的明显灰阶整页刷新。
- 固件 0.3.3-rc16：把参考稿 S03 文件操作面板、S05 传输方式选择与 S06 浏览器二维码传输页写入固件；提高阅读字体选择、统计、书签和最近阅读面板的文字、边框与选中状态对比度，并统一状态栏 WiFi 图标。
- 固件 0.3.3-rc15：连续 DU 翻页由 8 段 6 相位缩短为 6 段 5 相位，减少约四分之一动画扫描；定稿阶段对变化像素使用加强差分 GL16，压实新文字并继续擦白旧文字，改善翻页后的新旧页叠影。
- 固件 0.3.3-rc14：区分默认翻页与翻页动画。默认翻页保持单次 GL16；动画档复用示例工程连续 DU 状态机，以 8 段方向性边界同步擦除旧页、写入新页，下一页从右向左、上一页从左向右，最后只补抗锯齿与插图灰阶，避免再次整页刷新。
- 固件 0.3.3-rc13：阅读翻页改为单次 GL16 灰阶差分刷新。下一页一次提交，页眉、正文、插画和页脚在同一遍刷新中完成，不再横向分段擦入；普通缓存未命中也使用 GL16，仍每 14 页用 GC16 清理残影。
- 固件 0.3.3-rc12：降低首页、书架、文件管理和应用之间切换时的刷新存在感。普通切换保持差分 GL16；局部按钮、菜单、封面和弹窗刷新不再累计整屏清残影次数，系统页清理周期延长到 40 次整页切换，减少频繁黑白闪动。阅读页仍独立保持每 14 页清残影。
- 固件 0.3.3-rc11：参考 KOReader 软件擦入动画重做阅读翻页。下一页从右向左、上一页从左向右；中间帧使用灰阶窄条刷新，末帧以 GL16 定稿整页，清除原快速 DU 动画在文字和抗锯齿边缘留下的大量残影。
- 固件 0.3.3-rc10：阅读页底部章节与进度信息改用独立固定字号，开合阅读设置后不再随正文或系统字号变化；字号略微缩小，并显示“第几章 + 章节名”。同时加入阅读翻页动画选项、图像灰阶抖动、票根锁屏封面背景、WiFi 连接驻留、目录排版和 EPUB 插画兼容修复，并减少常规系统页面的整屏刷新。
- 固件 0.3.3-rc9：修复阅读字体滑块拖动时白块遮住滑轨；扩大滑块局部刷新范围并改用灰阶刷新。首页封面、书架分页和目录分页改为内容区局部更新，减少整屏刷新。目录页移除旧底部入口并重新排版。修复微信读书 HTTPS 登录链接被二维码校验错误拒绝。书籍名称统一取文件名，文件管理新增打开、复制、重命名和删除操作，连接 WiFi 时状态栏显示无线标识。
- 固件 0.3.3-rc8：修复 WiFi 扫描出现 `ESP_ERR_NO_MEM`。扫描前释放可重建的字形缓存，扫描结果移至外部内存并限制为界面实际可显示的 16 个网络；调低单用户传书不需要的无线缓冲预留，并增加失败阶段和剩余内存诊断。
- 固件 0.3.3-rc7：重写设备端 WiFi 扫描生命周期。扫描建立标准 STA 接口，兼容目标机的 remote/hosted WiFi 后端；一次性读取并去重结果，短暂状态错误自动重试，覆盖中国 2.4 GHz 信道 1–13。设置入口直接进入独立配网页，开放网络可直接连接，失败状态显示原因并提供重试。
- 固件 0.3.3-rc5：重构设备端 WiFi 配网页面并修复无线模块重复初始化后失效；设置入口直接进入配网，连接状态与已保存网络可正常更新。
- 首页固定使用六本书的基础网格，不再跟随个性书架；封面集中解码后一次刷新，避免每出现一本书就整屏闪烁。书架样式精简为深色书轨、亚克力挡板和半透明书袋三种。
- 系统设置新增全局对比度；字号、边距、行距支持端点逐级微调，修复最小行距反而增大的问题。
- 长按电源键显示双圆形按钮的重启／关机菜单；关机执行主机彻底断电，短按继续进入所选票根或壁纸锁屏。
- 首页恢复为六本书与继续阅读布局，书名保持单行且最多八个字，并同步展示所选书架效果；书架页取消书名，数量与页码移到底部中央。TF 目录补充文件夹、文件图形和容量信息，WiFi 传书页移除重复的 USB 入口，设置中的 WiFi 直接进入配网。应用图标加粗到适合墨水屏的清晰线宽，字体、书架与锁屏选项统一使用浅灰选中态。
- 底栏应用四宫格下移以统一视觉中心；应用页保留圆角外框，内部微信读书与设置改用和底栏一致的极简细线图标。
- 书架标题右侧新增「管理」并与「导入」并排，直接进入批量管理；应用页移除重复的图片、传输和图书管理入口，仅保留微信读书与设置。
- 系统二级页面共用的圆形返回键缩小，保留原触控热区，兼顾视觉比例和点击容错。
- 修复 TF 卡全目录浏览的分类计数越界；文件夹使用独立探测并优先排序，进入根目录后可以继续逐层浏览。
- 阅读设置补齐实际交互：字号、边距和行距可拖动并在松手后统一重排；阅读字体超过九个时可上下翻页；阅读统计的“阅读明细”和“最近30天”进入真实数据页面。
- 阅读页顶部恢复显示当前书名并移除圆形返回键及其隐藏热区；修正新版阅读工具栏仍误用旧按钮编号的问题，目录、书签、阅读统计和字体设置分别执行对应功能。
- 字体设置面板补齐可点击的字号、页边距、行距、阅读字体和晃动翻页控制，弹层使用各自命中区域，不再与底部工具栏重叠。
- 系统字体与阅读正文字体完全分离；首次进入产品界面前再次接管已挂载的 TF 系统字体，书名和系统控件不再跟随阅读字体。字体列表用浅灰底标记当前项。
- 首页与书架按内容版本、目录和 TF 挂载状态缓存扫描结果与封面，仅在导入、删除、换目录或存储状态变化后重扫。
- EPUB 插图改为读取文件签名识别 JPEG/PNG，兼容扩展名缺失或错误的图片；提高图片容量上限和浅色插画对比度。
- 阅读翻页取消延迟二次刷新；系统页面切换改用灰阶页刷，传输状态只刷新局部区域，减少整屏闪烁和等待。
- 状态栏使用圆角电池、左侧百分比、充电闪电和满电填充；应用页改为四列圆角图标，设置与各二级页统一返回、卡片间距及更清晰的系统字号。
- 文件管理显示 TF 目录并支持自选书籍/字体目录；USB 读卡直接进入挂载流程。票根与自选图片归入锁屏样式。
- 修复继续阅读书名尾部 `=This`、长书名截断、书架透明挡板辨识度及旧示例页面残留入口。

## 2026-09-30 · Codex

- 书架新增第四种半透明书袋样式，样式选择页使用四种实际书架首行的缩略图；票根锁屏在阅读中保留当前正文为背景，其他页面使用灰阶斜格纹。状态栏在充电时显示闪电标识。
- 应用页增加微信读书入口，目前支持已保存 WiFi 下的扫码登录、云端书架同步和退出登录；书籍缓存与本机阅读仍在开发中，尚未作为完整功能交付。
- 系统页面三颗触控键改为返回／首页／返回；阅读正文继续使用翻页与中键设置。系统小字约放大 1.2 倍，阅读正文大小不变。文字边缘、图标与分割线加深；书籍封面、插图、图片和票根壁纸提高灰阶对比度。
- 首页改为紧凑的继续阅读卡、近七天实测阅读时长与柱状图、书架摘要和四本最近阅读；书架提供默认网格、深色书轨、亚克力挡板三种样式，选项保存在设备上。
- 应用页改为浅色九宫格，框线加深并换用 Pico 灰阶图标；设置页改为卡片分组和较醒目的灰色图标，字号与间距集中在一个设置页。
- 亚克力书架挡板增加灰阶模糊和更明显的半透明覆盖，样式选择页共用同一绘制效果。状态栏使用圆角电池并在左侧显示百分比，系统界面字号略增。
- 书架切页后复用已扫描书目和封面，在传书、USB 退出或存储状态变化后重扫。重新生成内建字体子集，补齐当前界面文字；移除打开书籍时底部临时加载提示。
- 产品界面统一使用 Pico 名称。书架仅显示导入书籍，每页九本。底栏第四项改为应用，集中放置图片、传输、图书管理和设置。
- 本机书名编辑加入可点击定位、左右移动的插入光标，可在书名中间插入或删除文字。设置改为分组列表，加入 WiFi、蓝牙和日期时间入口。
- 手动设时与 WiFi 对时写入 PMU；开机读取 PMU 时钟恢复系统时间。WiFi 对时使用已保存网络，完成后关闭无线连接。

## 2026-09-29 · Codex

- 系统界面改用独立内建的 Noto Sans SC Regular 子集（OFL-1.1）；阅读正文仍可选择其他字体。修复书架设置入口的残留重绘与旧设置页代码。
- 设置页增加本机日期时间编辑（北京时间 UTC+8），保存后同步系统时间和 PMU；EPUB 章节题头使用目录标题，不再把正文前置引言当作章节名。
- 首页封面缓存在 PSRAM，返回首页时按路径、文件大小与修改时间复用，书籍顺序变化也可复用。
- 底部四栏图标改为细线风格：首页房屋、书架斜立书脊、文件夹与设置宫格。
- 首页阅读热力图改为最近 30 个自然日，使用实际阅读时长；设备时钟未同步时不显示虚构的日期数据。
- 加入首页、书架、文件管理、设置四栏导航；文件页集中书籍、图片、字体和 USB/Wi-Fi 传输入口。跨页面与进入阅读时执行整屏刷新。
- EPUB 章节第一页单独显示章节题头；书名改为本机拼音候选输入与保存，无需打开网页。阅读字体入口直接进入 TF 卡字体目录。
- 预览图使用苹方；固件保留可分发的无衬线后备字体，支持从 TF 卡加载兼容 TTF/OTF 文件，未随固件分发苹果字体。
- 固件 0.3.2：书架封面下只显示书名。首次打开书架或滑到未缓存的页面时先显示书架，再逐本补齐封面；生成的缩略图保存在 TF 卡隐藏目录，后续开机和翻页直接读取，换书后自动更新。
- 修正开机时误导性的「初始化内置存储」提示；既有内置分区仍按正常流程挂载，不会在每次开机格式化。字号和间距重排只测量字宽，减少额外的进度保存和字号二次刷屏；间距变化先快速刷新，闲置后整理灰阶。

## 2026-09-28 · Codex

- 固件 0.3.1：阅读页进度条改为圆角并移除外伸章节刻度；书架继续阅读区增加最近一本的封面、章节与页码信息。
- 票根锁屏上下使用裁纸缺口，背景由最近阅读的 EPUB 封面生成灰阶壁纸；无内嵌封面的书使用书名占位背景。

- 固件 0.3.0：书架移除底部翻页与菜单，改用左右滑动；顶部「设置」整合图书管理、图片和传输。
- 图书详情加入手动编辑书名入口；WiFi 网页可编辑 `book`、`books` 和内置图书的显示书名，独立保存而不改 EPUB 或进度。
- 票根锁屏显示最近阅读书籍、封面、进度和实测阅读统计；WiFi 网页同步设备时间以启用今日统计，时钟无效时明确提示。

- 主菜单收敛为书架、图片显示、传输；书架优先读取 EPUB 的书名、作者，显示封面和继续阅读入口，进入书架不再自动跳到正文。
- EPUB 正文支持 JPEG/PNG 图片独立成页；阅读设置增加字号、行距、段距和扩展字体。普通翻页改为快速差分，闲置后灰阶整理，周期整屏清残影。
- 传输页增加 USB 读卡模式，安全弹出后恢复 TF 卡本机挂载；USB 活跃时暂停本机卡访问和锁屏。
- 固件版本 0.2.0 已使用 DIO/80 MHz 写入 ESP32-S3，哈希验证通过，启动日志确认 SD 卡挂载与书架扫描。
- 增加图片对比页：内置灰阶测试图，读取 TF 卡 `pictures`/`images` 下的 JPG/PNG，并通过传书网页上传图片；图片切换时整屏灰阶刷新。
- 阅读中键单击进入页面设置，长按回书架；阅读页移除右下角返回图标；书架与字体列表忽略 macOS 的隐藏辅助文件。
- 书架同时扫描 TF 卡根目录的 `book` 与 `books` 文件夹，兼容已放入单数目录的书籍，传书仍保存到 `books`。
- 图书书架改为每页六本的封面网格；自动从 EPUB 2/3 的 OPF 声明提取 JPEG/PNG 封面，生成电子墨水屏灰阶缩略图，缺失时显示书名占位封面。
- 阅读工具栏加入宋体、黑体、楷体快捷切换，字体从 TF 卡 `fonts` 目录读取；附带三种开源中文字体。
- 提供合并固件镜像与基于 ESP Web Tools 的本地 Chrome 刷机页面。

## 2026-09-27 · UNSaWEN

- 图书阅读：支持 TF 卡或内置存储的 UTF-8/GBK TXT 与纯文本 EPUB，目录、逐书续读、字号、手势翻页及默认关闭的实验晃动翻页。
  Read UTF-8/GBK TXT and text-only EPUB from TF or internal storage, with a TOC, per-book resume, font sizes, gestures and optional experimental shake-to-turn.
- 图书管理：增加拼音/首字母/英文搜索、来源筛选、名称/最近阅读排序、详情，以及分别确认的单本/批量删除和清进度。
  Add pinyin/initials/English search, source filters, name/recent sorting, details and separately confirmed single/batch deletion or progress reset.
- WiFi 传书：支持设备热点和已有网络、触屏/网页配网、连接与网址二维码；网页管理当前存储，支持确认替换、取消和重试；停止后返回进入前的位置。
  Transfer over the device hotspot or an existing network, with touchscreen/web provisioning and connection/URL QR codes. Manage current storage in the browser, confirm replacements, cancel or retry uploads, and return to the entry location on stop.
- 可靠性：处理保存失败与部分成功重试、中文文件名和覆盖中断恢复；修复显示欠载后相位队列残留导致的死锁；检测 TF 失效后停止相关消费者并回退字体，插回后需显式重挂。
  Handle save failures, partial-operation retries, Chinese filenames and interrupted replacements. Fix a display queue deadlock after underrun; stop affected consumers and fall back fonts on TF loss, requiring explicit remount after reinsertion.
