# KimikoE4P-106 优化清单（2026-11-08 走查）

> 本文件为本地工作文档，不随 fork 推送。依据：全代码走查 + 2026-11-07 真机日志（刷新耗时、字体流、触摸帧、电源周期）。

## 优先级排序（性价比）

### ① 电源轨自适应保持 — 翻页延迟
- 现状：`components/epdiy/src/epd_power_guard.c` `IDLE_US=100ms` 空闲即断电；每次翻页重付上电序列（`board_metalio_e4p.c board_poweron_once`：2×50ms 延时 + PG 轮询 + TPS I2C ≈100–150ms）。
- 方案：连续翻页窗口内（距上次刷新完成 < ~2.5s）不断电；窗口过后照常 100ms 断电。
- 收益：每页省 100–150ms；电量代价可忽略。风险低。

### ② 字体卡片位图缓存 — 设置页体验 + SD 风暴
- 现状：`app_book.c draw_font_picker` 每帧重绘 8 张卡，每卡 `ttf_font_open` 开脸流 18–25MB（实测 draw=992ms、120MB/帧 SD 流量），并干扰活字体（2026-11-07 诊断的换脸问题根源）。
- 方案：每字体样张渲染位图缓存（选择态变化才失效），或 mmap 脸单次绘制。
- 收益：卡片页二次渲染瞬时；根治卡片换脸对阅读字体的干扰。

### ③ 插图页预解码 — 插图翻页延迟
- 现状：`app_book.c kick_prep` 下一页有图即放弃预绘（:3537）；插图页翻页全额付解码+绘制。
- 方案：下一页插图预算内后台预解码（文本页预绘的图片版）。
- 收益：插图页翻页从 ~600ms 降到 ~显示时间。

### ④ 触摸轮询自适应/中断 — 待机电耗
- 现状：`cst836u.c` 纯 I2C 轮询无 IRQ；主循环 5ms 一拍永不深歇（`app_loop.c LOOP_TICK_MS=5`）。
- 方案：查原理图确认 CST836U INT 引脚 → 中断驱动等触摸；未布线则无触摸 N 秒后轮询拉长到 20–50ms。
- 收益：待机续航最大单项。

### ⑤ 章内跳转解码缓存 — 目录/跳转速度
- 现状：`book_mobi.c/book_epub.c load_target` 每次跳转重新解压+解析整章（实测单章 218KB 每跳 ≈530ms+250ms）。
- 方案：已解码章节 LRU（html_text_t 复用，同章内跳转命中）。
- 收益：章内跳转只剩排版时间。需注意 PSRAM 预算。

### ⑥ DU 快速翻页档 — 翻页感知速度
- 现状：文本页默认 GL16 535ms；`display_refresh_policy.c` READER_BODY quick→DU 路径已存在但未暴露。
- 方案：用户可选"快速翻页"（文本 DU ~52ms + N 页整刷清理）；与 full_pages 联动调默认。
- 代价：残影加重，靠 N 页整刷兜底。

### ⑦ 死代码清理
- `app_book.c` `s_du_ms`/`s_du_area`（:535/:537 声明，:3883–3885 只写不读）。

## 长期项（高风险/大工程）

- **异步推屏**：535ms GL16 期间主循环阻塞，手势帧排队回放（滑块跟手性受限的根源）。present 移独立任务，需重设计 epdiy 并发与 prefetch 交还协议。
- **app_book.c 拆分**：7500+ 行单体，字体/跳转/面板状态纠缠；沿 reader_present/reader_prefetch 方向渐进抽离。
- **字体预览独立槽**：ttf_font 多槽补"预览槽选择+采样绘制"API，结构性根治卡片换脸。

## 已确认无需动

- 电量计（BQ SOC+电压曲线+EMA+NVS 容差，`device_power_metalio.c`）设计完善。
- 图片逐页预算解码、灰阶/抖动分离、三格式解码通道。
- 索引缓存（MOBI/TXT/EPUB 均有 book_index_cache）。
- N 页整刷清理、插图页强制 GC16、NORMALIZE 恢复波形。
- 宿主测试网（tools/ 数十个 host_test）。

## 进度

- [x] ① 电源轨自适应保持（已实现待验证）：`epd_power_guard.c` 两档空闲窗（单次 100ms / 连刷 600ms，间隔判定）+ `epd_power_guard_hint` API；`app_book.c turn_page` 翻页时提示 15s 阅读保持。epdiy.h/.c 加 `epd_power_hold_hint` 转发。
- [x] ② 字体卡片整卡位图缓存（已实现待验证）：`app_book.c` FONT_CARD_CACHE 48 项 FIFO，按（路径×选中态）缓存整卡 4bpp（ui_read_pixel 采集 / epd_draw_rotated_image 回放），字体集或卡尺寸变化整表重建。首渲染同旧，重渲染为位图拷贝。
- [x] ③ 插图页预解码（已实现待验证）：抽取 `decode_page_images(page, table, count)` 核心；新增 warm 缓存（下一页插图空闲预解码，PSRAM<2MB 跳过）；`prepare_inline_image` 命中即采用；`reader_prefetch_idle` 下一页有图改走预热；`free_book` 释放。
- [x] ④ 触摸 INT 门控（已实现待验证）：`touch_sampler.c` 采样任务加 `cst836u_int_asserted` 门控——无触摸且 INT 空闲跳过 I2C（100ms 兜底全读），空闲读 200/s→10/s；手势中全速不受影响。
- [ ] ⑤ 章内跳转解码缓存（未开始）
- [ ] ⑥ DU 快速翻页档（未开始）
- [x] ⑦ 死代码清理（已实现待验证）：删除 `app_book.c` 只写不读的 `s_du_ms/s_du_count/s_du_area` 三件套及其计数块。

## 第二批（2026-11-08 用户反馈三项）

### ⑧ 电轨提前关断 — 修复 ① 的落地缺陷
- 现象：① 的 15s 翻页提示形同失效，每翻一页仍重付上电序列。
- 根因：`epd_power_guard_session_end()` 最外层结束**无条件 `shut_down()`**；而 `display_present_end()` 每次刷屏都调一次，`shut_down()` 成功后又把 `hint_deadline_us` 清零——15s 提示当场作废。
- 修复：最外层结束后不再立即关电。已有排定空闲期则沿用（单刷 100ms / 连刷 600ms）；提示更晚则把 `wake_deadline` 顶到提示期限；无任何期限时按「提示剩余或单刷窗」兜底 `arm()`。显式 `off`/`flash_begin`/`deinit` 仍立即关电。
- 验证：`tools/epd_power_guard_host_test.py` 新增两组断言（组内 100ms 到期关电、900ms 提示撑过 600ms 连刷窗后到期关电），两个 diag 变体均通过。

### ⑨ 连翻期间震动反馈
- 现象：连续翻页时无触觉反馈。
- 根因：翻页刷屏期间（~535ms）主循环阻塞，点击按下沿全部进入触摸采样环回放；原回放路径不发震动，只有实时帧（`app_loop.c` 直读分支）发。
- 修复：`app_loop.c` 回放帧的按下沿同样 `haptics_pulse()`（`s_pulsing` 在飞去重把密集边沿收敛为不重叠单脉冲，不会变长震）。
- 验证：`tools/run_app_loop_host_tests.sh` 新增断言——3 次回放点击 = 3 记脉冲；多指帧不震。

### ⑩ 反向/菜单键打断连翻队列
- 现象：连点 10 次上一页，翻页期间点下一页，要等旧队列翻完才响应。
- 根因：回放严格 FIFO 排空，新按键排在整批同向点击之后；`book_remote_direction()` 也只取 BLE 环最后一个方向、一次一页。
- 修复：触摸环加前视/丢弃（`touch_sampler_peek/discard`）；回放发现积压里已有另一面板键的按下沿时，丢弃其之前的同向点击并立即执行最新按键。是否打断由页面钩子 `app_desc_t.key_interrupts_backlog` 判定：`app_book` 只在阅读正文、且「积压键是翻页 + 新键是反向翻页或工具栏」时打断；同向连点仍逐页执行，无钩子的页面保持严格顺序。
- 验证：`tools/run_app_loop_host_tests.sh` 新增断言——同向 10 连点仍 10 页；3 次上一页后接下一页 = 只执行 1 次下一页；无钩子页面 3+1 全部按序执行。

### 验证要点（第二批，烧录后）

1. ⑧：连续翻页第二页起明显更快；停止翻页约 15s 后电轨才关断（串口 `HV_OFF` 的 `hint` 非 0）。
2. ⑨：连翻时每页都有短震，且不会连成长震。
3. ⑩：连点上一页后点下一页，应立即在当前页向后翻一页（最多再走完手头那一页），而不是把剩余同向点击翻完。

## 第三批（2026-11-08 图片发灰发淡）

### ⑪ 插图页推黑被二次裁掉 — 中间调整体浅四档
- 现象：插图/图表区发灰发淡，而正文纯黑清晰。
- 根因：`display_reader_waveform.c build_bg_derivative()` 在水波纹基底（37 相）上对插图页又做了一次裁剪（`BG_TRIM_SAT_CUT=4`、`BG_TRIM_WHITE_SAT_CUT=3`），把黑饱和段砍掉 4 相。而 `e0470_boost_black_column()` 只补 **to=0**（纯黑）一列——正文因此照旧黑，灰阶目标却全部少 4 次推黑。灰阶落位是「先压到黑轨、再由灰尾抬到目标」，少一相推黑就整体浅一档；实测白→灰每对净推黑 12/11/10/10/10/9/9/8… 降到 8/7/7/6/6/6/5/5/4…，to=14 甚至变成净 -1（比纸白还浅）。
- 触发路径：`reader_present.c` 里插图帧按 `water_effect` 选表——水波纹开着（默认热门设置）走 `DISPLAY_WAVE_READER_IMAGE`（GL16，被裁）；关掉才走 `DISPLAY_REFRESH_READER_IMAGE`/`DISPLAY_WAVE_FULL`+GC16。另有面板开启、插图未解码完成等入口同样落在这张被裁的表上。
- 修复：`build_bg_derivative()` 增加 `sat_cut`/`white_sat_cut` 形参；插图页传 0/0，只保留原厂擦除上限 19、相等对保持和 24 拍白列。相数 33→37（仍是水波纹基底相数），插图页翻页时长约 +12%。
- 验证：`tools/reader_waveform_host_test.c` 新增断言——插图表 15×15 个非相等对的推黑次数与基底水波纹表逐一相等；`run_display_navigation_tests.sh` 全绿。

### ⑫ 顺带：渐进式 JPEG 插图固定 1/8
- 已确认限制（不是本次引入）：`jpegdec_shim.cpp` 对 SOF2 只解 DC 扫描，固定 1/8；封面 176×240 够用，整页插图会被放大 3–8 倍，同样表现为「朦胧」。
- 本次只加诊断：`decode_page_images()` 解码成功时若为渐进式，串口打 `progressive JPEG WxH decoded at 1/8 -> w x h`，便于区分「波形灰阶」与「解码分辨率」两种发淡。彻底解决需要引入支持渐进式的全分辨率解码器。

### 验证要点（⑪，烧录后）

1. 打开带插图/图表的 EPUB 页（水波纹保持开启），图的中间灰应明显加深，白底与黑线对比恢复。
2. 正文墨度、黑闪与翻页时长基本不变（插图页略慢约一成）。
3. 串口若出现 `progressive JPEG ... decoded at 1/8`，说明该书的图是渐进式、发虚另有其因。

### 验证要点（烧录后）

1. ①：连续翻页第二页起应更快；比例跳转滑块拖动应更跟手（不再每步付上电）。
2. ②：字体选择页首开正常慢，再进/翻页/选字体应瞬时；串口不再出现连续 8 张 "using …" 字体流。
3. ③：连续翻到插图页，第二页起插图翻页变快；串口可见 "warm page N" 与 "adopted warm decode"。
4. ④：正常点按/拖动无异常（点按延迟不变）；空闲时串口 cst836u FT 行频率明显下降。
5. ⑦：无行为变化（纯删除）。

## 第四批（2026-11-08 插图"看不清" → 渐进式 JPEG 全分辨率解码）

### ⑬ 渐进式 JPEG（SOF2）全分辨率解码（stb_image）
- 现象：插图/图表发糊，1px 框线、网格、坐标小字糊成一团灰，而正文（矢量字形）清晰。
- 根因（量化实锤）：`components/jpegdec/src/jpeg.inl:4974` 对渐进式硬编码只解 DC 扫描并 `iOptions |= JPEG_SCALE_EIGHTH` 强制 1/8。1px 黑线经 8×8 块平均后只剩 1/8 灰度：实测理想源 min=0，DC-only 后 min=221（接近背景 255）。封面 176×240 无所谓，整页插图放大到 684 宽就被抹糊。
- 修复：引入 `components/stb_image`（vendored v2.30，`STBI_ONLY_JPEG`，allocator 重定向 PSRAM）。`image_decode.c` 新增 `stb_progressive_gray()`，在 `jpeg_gray` 的 `sof==0xC2` 分支优先走 stb 全分辨率（`stbi_load_from_memory(..., req_comp=1)` 单通道灰 + 复用 `resample_gray8`），失败/超预算（`STBI_JPEG_PIXEL_BUDGET=200万px`）回退原 `jpegdec_gray`。基线 JPEG/PNG/WebP 路径不变。
- 验证：新增 `tools/progressive_jpeg_host_test.py`（Pillow 生成 SOF2 + 编译整个 image_decode.c + 验证 1px 框线 min<128），已加进 CI。实测 stb 解码 684×860 全尺寸、框线 min=0（对比 DC-only 221）。
- 构建：IDF 6.1 通过，bin 0x465570→0x469820（stb 约 +17KB）。
- 遗留：`book_image_file_grayscale`（文件流，用于阅读背景/壁纸）的渐进式分支仍是 DC-only 1/8，本次未改（插图走内存路径 `book_image_grayscale` 已修复）。
- 注意：加 stb 依赖触发 IDF 依赖重算，清理了 target 不匹配的 managed_components 残留（tinyusb/zlib）；`webp_host_test.py` 的 `concurrent` 在 macOS ASan 下崩溃是既有 flaky（WebP 路径不经过 stb），Linux CI 正常。

### ⑭ 插图中间调 gamma 压深（发灰发淡的第三层）
- 现象：⑬ 后图表清晰了，但整体仍偏淡（灰）。
- 根因：墨水屏 16 级灰阶物理响应中段偏亮（gamma 曲线），插图链路 `inline_ink_gray` 是纯透传（无任何 gamma/对比度校正），封面/锁屏走 `ui_contrast_gray`。抖动映射已验证线性（输入≈输出，误差<1级），所以"淡"在物理灰阶 gamma，不在抖动。
- 修复：`inline_ink_gray` 改为编译期 256 项 gamma LUT（gamma=1.4，`gray'=255*(gray/255)^1.4`），中间调压深约 2 级（128→97），黑(0)/白(255)端点不动、单调。常量表自包含、无 math 依赖（host 测试抽取函数体也能编译）。真机标定时重生成数组即可（改 gamma 值）。
- 注意方向：gamma>1 才是压深（x^gamma<x, 0<x<1）；之前误用 gamma<1（0.7）是提亮，已纠正。
- 验证：`book_ui_host_test.py` 断言改为「端点不动 + 单调不增 + 128<128」，通过。已烧录。
- 遗留既有问题（非本次引入）：`tools/reader_images_host_test.py` 的切片锚点 `typedef struct {\n    size_t skip;` 与当前 app_book.c 结构脱节（切片只切 116 字节无用 typedef），测试早已失效；`s_warm_images` 等③优化新增符号也未同步进抽取列表。需单独重写该测试抽取逻辑。

## 第五批（2026-11-10 天气替换为 Open-Meteo）

### ⑮ 天气直连 Open-Meteo（去掉私有后台依赖）
- 动因：原天气走 `CONFIG_PICO_COMPONENT_API_ORIGIN` 自建代理（`/api/v2/components` + locations/auto-location/forecast），后台属上游作者，独立维护后不可用。
- 选型：Open-Meteo 免费、无需 API key、有中文地理编码与两日预报；已验证 `geocoding-api.open-meteo.com/v1/search?name=杭州&count=20&language=zh`（返回中文名/经纬度/`country_code`/时区）与 `api.open-meteo.com/v1/forecast?...&daily=weather_code,temperature_2m_max,temperature_2m_min,relative_humidity_2m_mean,sunrise,sunset&timezone=Asia%2FShanghai&forecast_days=2`（daily 支持 `relative_humidity_2m_mean`，响应约 600B）。
- 实现：`weather_model.c` 删掉代理解析（`point/location/weather_location_parse/weather_locations_parse/weather_catalog_parse/weather_error_code/timestamp_field/weather_forecast_parse`），新增 `weather_om_locations_parse`、`weather_om_forecast_parse`、`weather_om_condition_code/text`。
  - 城市：按 `country_code=CN` 过滤 + 产品经纬度框（lat 0–54/lon 73–135），时区统一归一 `Asia/Shanghai`（新疆 tzdata 为 `Asia/Urumqi`，中国行政时间统一 +08），id 形如 `OM1808926`。
  - 预报：WMO→和风兼容码表（0→100/1→102/2→101/3→104/45,48→501/51,53→309/55,61→305/63→306/65,307/71→400/73→401/75→402/80→300/81→306/82→307/85,86→407/95→302/96,99→304），中文描述内置；`fetched=now/fresh=+1h/serve=+2h` 本地合成；日出日落按 `utc_offset_seconds` 折 UTC。
  - 服务：`http_get` 改收完整 URL，`origin_ok`/`url_trusted` 只放行两个配置主机；移除 catalog 探测与 auto-location（无 IP 归属地第三方依赖），搜索一次取 20 条本地分页。
- Kconfig：`PICO_COMPONENT_API_ORIGIN` → `WEATHER_GEOCODING_ORIGIN`（默认 `https://geocoding-api.open-meteo.com`）+ `WEATHER_FORECAST_ORIGIN`（默认 `https://api.open-meteo.com`）。
- 测试：`weather_model_host_test.c` 重写（地理编码过滤/时区归一/容量/空结果、预报两日/WMO 码表/日出 UTC/坏响应/有效期边界），`weather_service_host_test.c` 重写（可信源、守卫、搜索分页、缓存、取消、429/坏读），`weather_raster_host_test.py` 改用新解析。全部 PASS；`idf.py build` 通过（bin 0x469480，比代理版略小）。
- 已知取舍：`APP_WEATHER_DISABLED` 枚举保留但不再触发（无平台开关）；自动定位入口保留 UI 但只会提示手动选城。

## 第六批（2026-11-10 IP 定位 + 输入法城市词库）

### ⑯ 「定位」按钮改用 IP 归属地
- 选型：`https://myip.ipip.net/json`（HTTPS、免密钥、直接返回中文省市，93B/约200ms）。根证书链为 `*.ipip.net` ← TrustAsia DV TLS ECC CA 2024 ← TrustAsia TLS ECC Root CA ← Certum Trusted Network CA，后两者都在 `cacrt_all.pem`（`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y`），固件可验证。
- 解析：`weather_ip_city_parse` 取 `data.location[2]`（市），空则退回 `[1]`（省）；剥掉「市/省」后缀——地理编码只认「杭州」不认「杭州市」（实测带后缀返回 0 条）。
- 流程：定位 → IP 城市名 → `geocode_lookup` 取坐标 → 预报。开机首次无城市时自动尝试一次（`s_auto_attempted` 语义不变）。任一步失败都回退「未能确定城市，请手动选择」，不臆测城市。
- 配置：`CONFIG_WEATHER_IP_ORIGIN`（默认 `https://myip.ipip.net`，留空关闭定位）；`url_trusted` 额外放行该主机，空值不放宽允许列表。

### ⑰ 地理编码补「市」重查
- 问题：Open-Meteo 地理编码对 297 个地级及以上城市，首查只命中 218 个（73%）——「温州」「厦门」「泉州」「常州」直接 0 条，但「温州市」有。
- 处理：`geocode_lookup` 首查为空时补 `%E5%B8%82`（「市」）重查一次。实测覆盖 218+48=266（89.6%）。
- 仍缺 31 个（湖州、连云港、景德镇、商丘、许昌、哈密…）。**不做拼音回退**：ˈ宿州ˈ与ˈ苏州ˈ同音，回退会悄悄显示错城市天气，比「未找到」更糟。

### ⑱ 输入法城市词库（297 城）
- 问题：IME 的 DP 每音节只取最高频字，词表外的词全拼成乱码——`hangzhou`→「行周」、`changsha`→「长傻」、`qingdao`→「情到」、`urumqi`/`lhasa` 空结果。实测 38 个城市只有 3 个正确。
- 处理：`ui_ime_phrases.h` 追加 293 条城市（4 条已存在），pypinyin 生成拼音 + 逐条用地理编码校验城市名真实存在。排序按「直辖市 → 省会 → 其他」，同音组（suzhou/fuzhou/yichun/taizhou/yulin）按常用度人工定序，避免「宿州」压过「苏州」。
- 结果：297 城全命中，291 个排首位，6 个在前八；九宫格与全键盘同样生效；`keyboard_host_test.c` 加入城市回归断言（含同音排序与 T9）。
- 体积：bin 0x469480 → 0x46b6a0（+8736B），5MB 分区仍余 608KB。


## 第七批（2026-11-10 定位提速：内置城市坐标表）

### ⑲ 分阶段耗时打点
- `WEATHER_TRACE` 宏，仅 `ESP_PLATFORM` 编译（host 测试空宏），在 worker 开始/连网完成/IP/表查/地理编码/预报各打一条。
- 实测（优化前）：worker 0 → wifi 3403ms → IP 796ms（建连 695 + 传输 101）→ 地理编码 1939ms（建连 1695 + 传输 244）→ 预报 2334ms（建连 2102 + 传输 292）= 8521ms。
- 结论：**耗时全在建立连接**（DNS+TCP+TLS），传输只有 100–300ms。国内 ipip.net 同样一次 HTTPS 只 695ms，说明是跨境链路成本。

### ⑳ 内置 297 城坐标表（`main/services/city_coords.h`）
- 定位链路：IP 城市名 → 查表（2ms）→ 预报；表外才回落地理编码。定位 8521ms → 6632ms。
- 数据：Open-Meteo 地理编码 252 条 + OSM Nominatim 45 条（仅离线生成，固件不访问 Nominatim）。每条按省份交叉校验 admin1，防宿州/苏州串味。
- 生成器：`tools/gen_city_coords.py`（含省级行政区城市清单，可整表重跑）。
- 附带：Open-Meteo 未收录的 31 个地级市（湖州/连云港/景德镇/商丘/许昌/哈密…）现在可被定位选中。
- 测试：整表 297 条逐条过 `weather_location_valid`；「表内不联网」「表外才联网」两条分支各有用例。

### 剩余瓶颈
- WiFi 连接 + DHCP 3399ms 是固件层硬底；forecast 建连 2175ms 是跨境成本。物理下限约 4.2s。
- 再快只能靠「进天气页预热 WiFi」或「定位后短时保活」，都涉及电源策略，未实施。


## 第八批（2026-11-10 跟进上游 1.0.7–1.0.9）

- 采用「上游加为 remote + 逐提交 cherry-pick」而非手抄 diff；品牌已改、天气/OTA 已重写，但上游这几个版本没动天气源码，冲突可控。
- 已移植八项：前光上限 10%（1bd37220）、前光关灯状态修复（ef16a7ef）、页脚进度条显隐（0ac8e104）、书摘随进度（d4212afd）、关闭书内样式（6eee01d6）、残影优化（77c4a296）、传书队列与速率（db2bb4e9）、四向旋转与方向锁定（97ae9ba2）。
- **未移植**：底栏重设计（799be74d，用户否）、日报/打卡（2af2982c 等）、离线词典（98ce83d8/eb79c70b）、票根背景（c3995b4e）、日历锁屏（c8a19d26）、触控 V04（8e613b19）、九宫格（32e55c95）。
- 九宫格：**已决定暂时放弃**（用户 2026-11-10 确认），不再投入。客观原因：设置备份格式是**累积版本链**（v36 禁触、v37 票根、v38 日历、v39 九格），只移植 v39 会在 v35 与 v39 之间留空档，且测试与底栏重设计耦合。若将来重做，必须先补 v36–v38 中的中间功能，或把九格重编号为 v36 并单独验证备份升级/降级路径。
- cherry-pick 的三个坑：① 上游部分提交把**未移植的词典模块**一起带进来（分支合并痕迹），需手工摘除并在测试里删对应桩；② 冲突解决后仍有文件残留 `<<<<<<<` 标记被 `git add -A` 提交，靠全仓扫描才发现；③ 测试桩依赖新产品符号（release_warm_images、reader_orientation_*、s_reader_progress_bar），需逐套件补。
- 验证：idf.py build 通过（0x473160，余 577KB）；run_reader_baseline 71 项通过，余 5 项为移植前既有失败、7 项为环境安全删除拦截。
