# 组件库与天气 / Components And Weather

## 使用 / Use

首页分为上下两个等高区域，可分别选择「最近阅读」「天气」「阅读手账」或「留白」。非留白组件各只能出现一次；选择已在另一区域的组件时，两区自动交换，留白可以重复。通过留白区域的设置图标、长按任一区域，或设置页「阅读背景」下方的「组件库」进入配置。天气去除外框和圆角，铺满所在区域并直接衔接底部导航。组件库、天气设置及城市页底栏均仅保留中间的首页按钮，两侧无隐藏返回热区。点「今日 / 明日」本地切换，不额外请求天气。旧组件、旧月阅读记录与闭环目标不恢复；新增手账的独立采集、日历和存储边界见[阅读手账](READING_JOURNAL.md)。

天气底部只保留36像素的刷新图标，不再显示「刷新」文字；图标中心位于区域左侧632像素处，点击范围为64×64像素，避开导航栏的扩展热区；最近阅读右上角的设置图标及点击入口也已移除，天气设置通过长按区域或设置页组件库进入，原底部设置图标及其独立热区移除。

Home has two equal regions, each choosing Reading, Weather, Reading journal or Empty. Real widgets are unique: selecting one already in the other region swaps both choices; Empty may repeat. Open configuration using the settings icon in Empty, a hold on either region, or the library below Reading background in Settings. Weather is rectangular, borderless and edge-to-edge, meeting the footer directly. Configuration, weather settings and city footers retain only Home, without hidden side hit areas. Day switching is local and never submits a weather request. Retired widgets, old monthly records and goals remain removed; the journal guide defines the new independent recording/calendar/storage contract.

The weather footer shows only a 36-pixel refresh icon centered at x=632 within the region, with a 64×64-pixel hit area outside the navigation touch margin. The Refresh label and the Reading settings icon/tap target are removed. Open weather settings with a hold or through the component library in Settings; the old footer settings icon and its independent hit area are removed.

「天气设置」提供独立的「自动刷新」开关，默认开启，周期六小时，关闭后仍可手动刷新、搜索和选择城市。仅天气已添加到首页且首页或非城市组件页处于前台时自动检查；移除天气停止自动请求，不中断阅读或为天气唤醒待机设备。错过周期后回到这些页面再补更新。周期以服务器原始预报更新时间为准，重启不重新延长；后台有效期更短、缓存缺失或跨日缺少明日预报时会提前补取。失败至少冷却五分钟，旧数据过期后仍隐藏，不为凑满周期延长有效期。

Weather settings has an independent Automatic refresh toggle, on by default at six-hour intervals. Turning it off preserves manual refresh, search and city selection. Automatic polling requires a placed weather widget and foreground Home or non-city component pages. Removing weather stops automatic requests; weather never interrupts reading or wakes standby. Missed refreshes resume on returning to these pages. Timing uses the original server timestamp across reboots. Earlier expiry, missing cache or a missing tomorrow can trigger an earlier fetch. Failures retain a five-minute minimum cooldown; expired data gains no extra lifetime.

尚无城市时点「定位」即可：设备向 `CONFIG_WEATHER_IP_ORIGIN`（默认 `https://myip.ipip.net`）询问当前出口 IP 所属城市，拿到中文城市名后剥掉「市/省」后缀，再走地理编码取坐标；每次开机最多自动尝试一次。也可以点城市名称手动搜索：输入中文或英文（地理编码按中文返回名称），点「搜索」并选择带市、省行政区的候选。定位或检索失败时提示手动选城，不臆测城市。一次请求取回最多 20 条，本地每页八项，城市名36px、行政区24px分两行显示，每行84px且整行可点，列表利用输入框至底部翻页区之间的空间；底部左右箭头的点击范围为72×64px，显示总数与页码。杭州可查到城市及 13 个区县，共 14 个地点。进入城市页不展开键盘，点输入框才展开，点空白处收起且保留文字及未确认拼音；展开键盘时暂不显示或响应结果与翻页，收起后恢复。有效搜索和自动定位会收起键盘。城市以 `OM` 前缀加地理编码地点 ID 持久保存，普通刷新不重新定位。只保留中国大陆结果，时区统一归一到 UTC+8，不提供 GPS 或精确定位权限。

With no selected city, tap Locate: the device asks `CONFIG_WEATHER_IP_ORIGIN` (default `https://myip.ipip.net`) which city the egress IP belongs to, trims the 市/省 suffix from the Chinese answer and geocodes it into coordinates, at most once per boot. Manual search stays available in Chinese or English (the geocoder returns Chinese labels). A failed locate or lookup asks for a manual choice instead of inventing a city. One request returns up to 20 hits, sliced into eight-row pages locally; each row has a 36-pixel name, 24-pixel administrative subline and full-row hit area. Paging arrows use 72x64-pixel hit areas beside totals/page numbers. The keyboard opens on field taps and dismisses without losing input. Results and paging are hidden and inactive while it is open; valid searches dismiss it. Cities persist as an `OM` prefix plus the geocoding id; normal refresh never relocates them. Only mainland-China hits are kept and every timezone is normalised to UTC+8, without GPS permissions.

共用键盘的键间背景、辅助键和首个候选均使用白底黑字，首选候选以双线框区分，候选字号32px。按键按下仍有局部黑白反馈。候选栏累计变化三次并停手400ms后，沿现有局部全像素GL16出口整理词栏；城市页保留这一刷新意图，不降为差分刷新。连续输入、按住及多点触摸期间不整理，不增加整屏清理或修改波形、电源策略。实际清晰度和累计残影仍需实屏验收。

Shared keys, utility buttons and the first candidate use white paper/black text; a double outline marks the first candidate, with 32-pixel candidate text. Press feedback remains local and binary. Three candidate-strip changes followed by a 400ms touch-free pause use the existing full-pixel local GL16 settle, which the city page must not downgrade to differences. Continuous input, held keys and multitouch postpone cleanup, without whole-screen cleanup or waveform/power-policy changes. Optical clarity and accumulated residue still require panel acceptance.

定位不走检索：`main/services/city_coords.h` 内置 297 个地级及以上城市的中心坐标与行政区，IP 城市名精确命中就直接取坐标，只有表外地名才回落到地理编码。实测一次定位里地理编码的建连就要 1939ms（传输仅 244ms），查表是 2ms；定位总耗时因此从 8521ms 降到 6632ms。该表由 `tools/gen_city_coords.py` 生成，每条按省份交叉校验，避免同音城市串味。
Locating never searches: `main/services/city_coords.h` carries centre coordinates and administrative names for 297 prefecture-level cities, so an exact IP-derived name yields coordinates directly and only off-table names fall back to geocoding. In one measured locate the geocoding handshake alone cost 1939 ms (only 244 ms of it was transfer) while the table lookup took 2 ms, cutting the action from 8521 ms to 6632 ms. The table is generated by `tools/gen_city_coords.py` and every row is cross-checked against its province so homophone cities cannot be mixed up.

城市检索直接使用 Open-Meteo 地理编码（`geocoding-api.open-meteo.com`），`language=zh` 取中文名；不再维护本地城市字典，也不需要任何 API 密钥。命中后按 `country_code=CN` 与产品允许的经纬度框过滤，境外结果直接丢弃。该源把一部分城市只存成「温州市」这类带后缀的全名，因此首查为空时会补「市」再查一次（`geocode_lookup`）；实测 297 个地级及以上城市由此从 73% 提升到 89.6%，仍缺的 31 个只能等源库补齐，不做拼音回退——「宿州」与「苏州」同音，回退会悄悄显示错城市的天气。

City search calls the Open-Meteo geocoder (`geocoding-api.open-meteo.com`) with `language=zh` for Chinese labels; there is no local dictionary and no API key. Hits are filtered by `country_code=CN` plus the product's lat/lon box, dropping overseas results. That source stores some cities only under their suffixed full name (「温州市」), so an empty first pass retries with 市 appended (`geocode_lookup`); across 297 prefecture-level cities this lifts coverage from 73% to 89.6%. The remaining 31 wait for the source, and there is deliberately no Pinyin fallback: 「宿州」 and 「苏州」 share a reading, so a fallback would silently show another city's weather.

温度为当日最低/最高摄氏温度，以 `/` 分隔；湿度为白天预报湿度，不是实时传感器读数。日出为地平线上的半轮太阳；日落为半空完整太阳及向地面的箭头。缺失事件显示 `--:--`。全区背景区分晴、多云、阴、雨、雪、雨雪混合、雾霾、沙尘和雷雨；雾和霾共用背景，天气文字保留各自原描述。热、冷和未知代码保持中性，不推断天空状况。

Temperatures are daily minimum/maximum °C separated by `/`, and humidity is daytime forecast humidity, not a live sensor. Sunrise uses a half sun at the horizon; sunset uses an airborne sun and downward arrow. Missing events show `--:--`. Region-wide scenes distinguish sun, cloud, overcast, rain, snow, mixed rain/snow, fog/haze, dust and thunderstorms. Fog and Haze share one background while retaining their original condition labels. Hot, cold and unknown codes stay neutral without inferring sky conditions.

按和风[天气现象](https://dev.qweather.com/docs/api/weather/weather-conditions/)映射：`104` 为阴，`404–406` 为雨雪混合，`503/504/507/508` 为沙尘；雾 `500/501/509/510/514/515` 和霾 `502/511–513` 共用雾景。多云、小雨、雪仍沿用原背景，不按雨雪强度另外新增图片。

Following QWeather's [condition list](https://dev.qweather.com/en/docs/api/weather/weather-conditions/), `104` selects Overcast, `404–406` mixed rain/snow and `503/504/507/508` Dust. Fog `500/501/509/510/514/515` and Haze `502/511–513` share the fog image. Cloud, Rain and Snow retain their images, without extra images for precipitation intensity.

雾霾以林梢被低雾带遮挡表现低能见度，不使用全区平行斜线或独立霾图。雷阵雨使用独立云雨构图，闪电主干从云底贯穿到组件最下沿，允许自然分叉，以不规则转折、宽窄变化和内侧亮芯表现放电通道。背景仍沿用固定灰度的1bpp资源，不改变刷新策略；host图不等同于实屏效果。

Fog and Haze use treetops obscured by low mist banks, without region-wide parallel slashes or a separate Haze asset. Thunderstorms have a separate cloud/rain composition and a discharge trunk running from the cloud base to the widget's bottom edge, with natural side channels, irregular turns, varied width and inner bright cores. Backgrounds retain fixed-gray 1bpp masks and existing refresh policy; host previews are not panel acceptance.

## 平台接口 / Platform Contract

天气使用公开的 Open-Meteo：`CONFIG_WEATHER_GEOCODING_ORIGIN`（默认 `https://geocoding-api.open-meteo.com`）做城市搜索，`CONFIG_WEATHER_FORECAST_ORIGIN`（默认 `https://api.open-meteo.com`）取两日预报，`CONFIG_WEATHER_IP_ORIGIN`（默认 `https://myip.ipip.net`，可留空关闭定位）提供 IP 归属城市。两个源都必须是无路径、无端口、无凭据的 HTTPS 主机名，设备只接受这两个前缀下的 URL。请求不带 Cookie、设备注册或任何 API 密钥，也不改变 OTA 下载源。预报有效期在设备本地合成（1 小时新鲜、2 小时失效）。WMO 天气码在设备内映射为原界面使用的和风兼容码与中文描述。

Weather uses public Open-Meteo: `CONFIG_WEATHER_GEOCODING_ORIGIN` (default `https://geocoding-api.open-meteo.com`) for city search, `CONFIG_WEATHER_FORECAST_ORIGIN` (default `https://api.open-meteo.com`) for the two-day forecast, and `CONFIG_WEATHER_IP_ORIGIN` (default `https://myip.ipip.net`, empty disables locating) for the IP-derived city. Both must be bare HTTPS host names with no path, port or credentials, and the device only accepts URLs under those two prefixes. Requests carry no cookies, registration or API key, and OTA sources are unchanged. Forecast validity is synthesised on-device (fresh for one hour, expired after two). WMO codes are mapped on-device into the QWeather-compatible codes and Chinese labels the existing UI already uses.

产品配置使用系统可信证书束，不内置本地联调 CA；已移除联调用 USB 控制命令、页面/刷屏性能插桩及天气逐请求测时日志。独立开发证书、配置和测试包仅保留在忽略目录，不进入源码或已发布下载。

Product builds use the system trust bundle without local development CAs. Local USB control commands, page/display timing probes and per-request weather timing logs are removed. Development certificates, configurations and test images stay in ignored directories, outside source and published downloads.

第 1 版结构同时返回两日，太阳事件与期限为 UTC。预报请求仅带 `locationId`，核对响应版本、来源、所选 ID、标准坐标、时区、日期、数值范围和期限；拒绝超长、截断、过深嵌套或坏响应。搜索明确提交后才请求，带 `page`（从零开始）及 `pageSize=20`，校验总数、页码及该批项数。界面仍每页八项，已取批次暂存RAM，同一查询翻到已缓存结果不启动网络任务、不重连WiFi；不足20条的结果一次取齐。更多结果首次翻到缺失批次时才补取，回翻不重复请求，不在首搜时阻塞下载整个城市字典。总数上限4000，按需分配批次；改查询、重新搜索、选城或离页释放缓存，后台旧查询结果不回填。查询须为2至64个字符，界面输入缓冲为128字节。所有用户共享平台按地点 ID 的默认六小时预报缓存，不按个人坐标分散缓存。

Schema 1 returns both days with UTC events/deadlines. Requests use only `locationId`; responses validate that ID, canonical coordinates, timezone, dates, ranges and deadlines. Explicit search requests validated 20-item batches while the UI keeps eight rows per page. Query-scoped RAM batches serve cached pages without a worker or WiFi reconnect; up to 20 matches arrive together. Larger searches fetch only missing batches when needed, retain them for return navigation and never download the entire dictionary before showing results. At most 4000 results are supported with allocation on demand. Query edits, explicit resubmission, city selection and exit release batches; stale workers cannot restore them. Queries require 2-64 characters within 128 bytes. All users share the platform's default six-hour cache per location ID, never per personal coordinate.

天气校验、今日/明日选择及自动刷新共用 `app_weather_now()`：完整且校验通过的可信 HTTPS 预报响应提供标准 GMT `Date` 时间头，以收到该头时的单调计时推进天气专用 UTC，不写系统时钟或 RTC。正文下载及 UI 交接等待均计入经过时间，后续旧响应不能倒退已建立的参照。目录、搜索、定位、失败或取消的响应不能建立参照；缺失、格式错误或重复的 `Date` 不可信，改用已有天气参照，尚无参照时仍按设备时钟严格校验。保留超过参照时间120秒的未来预报拒收、原始期限及最长六小时限制，不用 `fetchedAt` 冒充当前时间。

Validation, day selection and automatic refresh share `app_weather_now()`. A complete, validated forecast from the trusted HTTPS origin supplies a standard GMT `Date` header, advanced using monotonic time since header receipt without setting the system clock or RTC. Body download and UI handoff delays count, and later old responses cannot move an established reference backward. Catalog, search, location, failed and cancelled responses cannot establish it. Missing, malformed or duplicate `Date` falls back to the existing weather reference, or strict device-clock validation if none exists. Forecasts over 120 seconds into the reference's future remain rejected, with original deadlines and the six-hour limit intact; `fetchedAt` never substitutes for current time.

地点 ID 为 1 至 16 位大写字母或数字的字符串，不限定九位数字；官方 `F4F1`、`CA3` 等地点可正常选择、请求和保存。平台仍须核对 ID 存在于官方字典中。

Location IDs are strings of 1-16 uppercase ASCII letters or digits, not necessarily nine digits. Official IDs such as `F4F1` and `CA3` can be selected, requested and persisted. The platform still requires dictionary membership.

平台默认停用，须先配置合法凭据并完成平台测试。官网署名和特殊设备许可仍须向供应商确认，不能把“只在官网署名”视为已获许可。不在卡片中擅自添加来源文案，不生成样例预报冒充服务成功。

Activation requires legal credentials and successful platform checks. Website attribution and special-device permission still require supplier confirmation; website-only attribution is not assumed approved. Device attribution text is not silently added and fixtures never substitute for unavailable weather.

## 网络与生命周期 / Networking And Lifecycle

仅首页或组件页驱动服务，无常驻联网任务、不主动唤醒。已有可用 STA 只借用、不启动或停止；连接中、热点和错误会话不抢占。离线时短连已保存 WiFi，仅关闭自己成功创建的会话。不打开传书服务器、不访问书籍存储、不改 WiFi 凭据。

Only Home/component pages drive weather, without permanent networking or sleep wakeups. Ready STA is borrowed and never started/stopped by weather. Connecting/AP/error sessions are not taken over. Offline requests briefly connect saved WiFi and close only their own session, without upload servers, book access or credential changes.

天气忙时自动校时/OTA避让；开始前取消自动校时，OTA忙时不启动。退出页面、锁屏、关机准备和快捷 WiFi 前取消并等待资源交还，不强杀任务。单次任务预算45秒；HTTPS连接使用异步握手，最长6秒，每20毫秒检查取消；响应头最多等待5秒，正文连续5秒没有数据则失败，收到正文会重置无进度计时但不延长会话总预算。单次读取等待最多200毫秒，正文每次最多512字节，暂时性EAGAIN在窗口内重试，实际错误或截断立即失败。响应最多8192字节，不跟随重定向。取消仍可能等待当前 DNS/读写调用；真实延时和峰值内存需设备验证。

Automatic clock/OTA work yields while weather is busy. Weather first cancels automatic sync and never starts during an online update. Exit, lock, shutdown preparation and quick WiFi cancel and await cleanup instead of killing tasks. The session budget remains 45 seconds. Asynchronous HTTPS connection establishment permits up to six seconds with cancellation checked every 20 ms. Headers allow five seconds; body progress resets a five-second inactivity window without extending the overall budget. Individual reads wait at most 200 ms with body chunks capped at 512 bytes. Temporary EAGAIN retries within the window; real errors and incomplete EOF fail. Responses are capped at 8192 bytes and redirects disabled. Cancellation may still wait for DNS or an in-flight read/write; actual latency/memory peaks need device validation.

自动失败冷却至少5分钟。蓝牙翻页器已连接、低电量未充电、日期无效或内存不足时不联网。用户刷新仍走公共缓存 GET，不调用管理端强刷接口。

Automatic failures cool down at least five minutes. A connected Bluetooth turner, low unplugged battery, invalid clock and allocation failure prevent networking. User refresh uses the public cached GET, never an admin force-refresh endpoint.

天气参照只在本次开机的 RAM 中保留，重启后先以设备时间校验持久缓存，取得下一次正常预报才重新建立。系统日期无效时仍需先校时；HTTPS 证书验证依赖系统时钟，本机制不替代 SNTP 或修复无效系统日期。

The weather reference stays in RAM for the current boot. After reboot, persistent cache is checked against device time until a new valid forecast establishes a reference. An invalid system date still requires clock synchronization: HTTPS certificate validation depends on system time, so this mechanism does not replace SNTP or repair invalid system dates.

## 缓存与绘制 / Cache And Painting

保留服务器原始 `fetchedAt`、`freshUntil`、`serveUntil`，最长六小时，不因读取或失败延长。失败时只展示有效期内缓存并标记；到期隐藏数值。跨午夜按日期匹配，旧“明日”可成为今天，但缺少新的明日就显示缺失，绝不重新命名旧日期。服务器明确停用后撤下数值并尝试删除持久缓存。

Cache retains original server timestamps, with at most six hours of validity and no extension on reads/failures. Only valid cache may survive failures, with labels; expired values disappear. Midnight selection matches actual dates: old tomorrow may become today, but missing new tomorrow stays missing. Explicit server disable clears visible values and attempts persistent cache deletion.

天气开关、城市与预报保存在独立 NVS 命名空间 `kiiko_weather`，仅 UI 任务写入并受 `epd_flash_begin/end` 保护。选择写入失败不伪装保存成功；缓存写入失败不影响当前 RAM 显示。该命名空间**不包含在设置/书籍备份恢复中**。不复用或清除旧组件兼容字段。换城丢弃旧预报；搜索结果在改查询或离页后失效。

Weather preferences/city/forecast use `kiiko_weather` NVS with UI-only EPD-flash-protected writes. Failed preference writes never pretend success; failed cache writes retain current RAM display. **Settings/book backup/restore excludes this namespace.** Retired compatibility fields remain untouched. City changes invalidate forecasts; query changes/exit invalidate transient search results.

首页布局单独保存在 `read_pico/home_regions`，两个区域一次提交，失败时都保持原值。首次没有新设置时，上方为最近阅读，下方继承旧天气显示开关；读取兼容不立即写入。v33 设置备份包含上下组件布局，恢复前校验版本、唯一性和校验值，提交成功才更新当前布局；v1–v32 备份不含布局，恢复时保留当前选择。天气城市、自动刷新偏好和预报缓存仍不随备份迁移。

Home layout uses `read_pico/home_regions`, committing both choices together and retaining both on failure. Without a new setting, reading starts above and the old weather visibility determines the lower region, without a migration write. Version 33 settings backups include the layout, validating schema, uniqueness and checksum before writes and updating live choices only after commit. Versions 1–32 preserve the current layout on restore. Weather city, automatic refresh preference and forecast cache remain outside the backup.

天气偏好schema 3保存地点 ID、行政区信息及自动刷新开关，缓存schema 2保存对应预报。该功能仍处本地开发，不迁移旧开发格式；首次使用新格式需要重新选城。仅在用户修改偏好或保存城市时写入，不在每次轮询落盘。

Preference schema 3 stores official IDs, administrative metadata and the automatic toggle; cache schema 2 stores its forecast. This unreleased feature does not migrate earlier development formats, so select the city again. Writes happen on preference/city changes, never each poll.

`ui_weather_draw()` 纯绘制，无联网、持久化或分配，显式接收684×472区域。首页两区从y=168开始，于y=1112直接接导航；公共导航仅绘制y=1112以下区域，不额外清白或保护上方24px内容。进入页面时准备684×472的4bpp暂存（161424字节PSRAM），离页释放，不另分配整屏缓冲。内存不足则保留清晰白底内容、略去装饰。复用系统字体，不增加内建字库。九种固定背景以1bpp只读资源保存，合计310464字节Flash，雾霾共图，不额外占用PSRAM；设备直接绘制原纹理，文字留白按连续笔画合并。首页切日只重画并呈现天气所在区域，不重画页眉或最近阅读。分钟时钟仍独立局部更新，不改变波形或电源策略。

内容刷新截止与固定导航起点统一为y=1112，所有页面普通整页/局部差分直接覆盖y=1088–1111，不再把这24px作为固定区或另留白带。32列物理对齐仍由底层处理，固定导航取真实已显示像素。仅全像素清理向内对齐到y=1088并对余下内容补差分，避免驱动固定导航；导航图标及横条位置不变。

Content differences and fixed navigation share y=1112 across all pages, so ordinary page and area updates directly include y=1088–1111. Only full-pixel cleanup aligns inward to y=1088 and completes the remaining content with differences, preserving fixed navigation pixels and icon positions.

`ui_weather_draw()` only paints into an explicit 684x472 region, without networking, persistence or allocation. Home regions start at y=168 and meet navigation at y=1112. Shared navigation paints only y=1112 and below, without clearing or protecting 24 extra content rows. Entry prepares a 161424-byte region-sized scratch; exit releases it. There is no extra full-screen buffer. Allocation failure retains white-background content. The active font is reused. Nine fixed 1bpp backgrounds use 310464 bytes of read-only Flash with a shared Fog/Haze image and no extra PSRAM. Firmware stamps original textures and combines contiguous ink halos into spans. Day switching presents only the weather region, leaving the header and reading untouched. The minute clock stays independent; waveforms/power policy are unchanged.

背景原图在 `main/assets/weather/`，资源由 `python3 tools/generate_weather_backgrounds.py` 无损打包；`--check` 校验可复现性，不手改二进制。/ Source masks live in `main/assets/weather/`; losslessly pack them with `python3 tools/generate_weather_backgrounds.py`, or verify with `--check`. Do not hand-edit the binary.

## 验证入口 / Verification

`python3 tools/weather_host_test.py` 或 `python3 tools/run_reader_baseline.py weather` 覆盖真实解析/缓存、取消服务与NVS交接、九种背景及中性画面/双旋转/区域边界/内存降级，并逐像素验证四个霾代码与雾景共图。固定样本不是定位、供应商接入或实屏证据。产品构建仍须 IDF6.1/S31 `--preview`；部署、供应商实测和刷机分别需要授权。

The weather host suite covers real parsing/cache, cancellable service/NVS handoff, nine backgrounds plus neutral, two rotations, bounds and allocation fallback, and pixel-identical rendering for all four Haze codes versus Fog. Fixtures are not location/provider/optical evidence. Product builds require IDF6.1/S31 `--preview`; deployment, provider tests and flashing require separate authorization.

时间回归还覆盖 GMT 时间头与两分钟边界、设备时钟偏快/偏慢、系统时间跳变、正文及交接延迟、旧参照不倒退、原始期限到期、跨日缺明日及重启回退；首页与天气设置页均验证传入天气专用时间。/ Clock regressions cover GMT headers, the two-minute boundary, skew in both directions, system-clock jumps, body/handoff delays, nondecreasing references, original expiry, missing tomorrow at midnight and reboot fallback; Home and weather settings both verify the weather-specific draw time.

`python3 tools/weather_raster_host_test.py --output-dir build/weather-preview` 在回归通过后导出十类天气（雾霾共图）及中性画面的今日/明日共22张原生组件图、五张对照图和PNG压缩包，使用当前固件绘制与内置字体，不访问网络。

After regressions pass, `python3 tools/weather_raster_host_test.py --output-dir build/weather-preview` exports 22 native widget images (ten conditions with shared Fog/Haze, plus neutral, Today/Tomorrow), five comparison sheets and a PNG archive using current firmware drawing and the built-in font, without networking.
