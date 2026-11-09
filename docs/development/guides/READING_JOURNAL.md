<!--
SPDX-FileCopyrightText: 2026 chopper1026
SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
-->

# 阅读手账 / Reading Journal

## 使用 / Use

在首页长按组件区域，或从设置的「组件库」选择「阅读手账」。它与最近阅读、天气一样占用一个684×472等高区域，同类组件不能重复；默认布局不变。是否放到首页不影响阅读记录采集。

Choose Reading journal by holding a Home region or opening the component library in Settings. It occupies one 684x472 region and cannot be duplicated, like Reading and Weather. Existing default layouts remain unchanged; recording does not require placing the widget.

首页只展示今天的阅读时长、书籍数、章节数、翻页数和最多两本书。点击插画以外的组件区域进入详情，默认选中今天；点击当月日历中的日期查看那天的记录。未来日期灰显且不可选，有阅读的日期带方点，今天描边，选中日期反色。详情每页两本书，更多书目可用列表下方箭头或在书目区左右滑动查看；换日期回到第一页。底栏只有中间首页图标，两侧不显示按钮，也没有隐藏点击区。盖板左右键返回，中键回首页，中键长按全刷。

Home shows only today's duration, book/chapter/turn counts and at most two books. Tap outside the illustration to open details with Today selected, then choose a date in the current-month calendar. Future dates are muted and disabled; reading dates have dots, Today an outline and selection an inverted fill. Details page through two books at a time using arrows above the footer or swipes over the book list; date changes reset paging. Only the center Home footer is active. Side hardware keys return, the middle key goes Home and a middle hold refreshes the page.

首页和详情共用六幅像素场景：沿途有书、街角书店、纸上花园、月面观测站、唱片聆听角、灯塔书屋。点击插画只换图，不进入详情或改变选中日期；同日手动选择会保存。下一天从当前图继续前进一幅，间隔多天按天数推进，跨月不断序列；日期回拨保留当前图并重建日期基线。每日轮换在下一次前台轮询时发生，不为换图唤醒待机设备。空记录仍显示当前选中的场景；旧图及阅读角空景不再内置，已保存的六图序号继续有效。

Home and details share six pixel scenes: Books Along The Way, Corner Bookshop, Paper Garden, Moon Observatory, Vinyl Listening Corner and Lighthouse Library. Tapping art changes only the scene, preserving navigation and date selection. Manual choices persist for the day; subsequent days advance from the current image, including elapsed days and month changes. Clock rollback preserves the image and rebases its date. Rotation happens on the next foreground poll without waking standby. Empty journals use the selected scene too; old scenes and the empty corner are no longer bundled, and saved six-scene indices remain valid.

## 统计口径 / Measurement

- 日期固定使用UTC+8，与当前设备日期一致；有效年份为2024至2099，未校准时显示提示，不把无日期的阅读补记到任意一天。系统时钟不可用时可借用已校准RTC。/ Dates use fixed UTC+8 and years 2024-2099. Invalid time displays a notice instead of inventing dated records; a calibrated RTC can supply time when the system clock is unavailable.
- 成功开书才建立书目记录，成功翻页才增加一次翻页，前后翻页均计入；边界、加载失败和无效操作不计。书籍按完整路径与文件大小关联，改路径或大小会作为另一书目，不以同名合并。/ Successful opens establish records and successful forward/backward turns increment counts; boundaries and failures do not. Identity uses full path plus size, not title, so changing either creates another identity.
- 章节数是每天每本书到访过的不同章节数，不是读完章数；同章反复翻页不重复计章。月章节数为各天章节数之和，同一章节在不同天阅读会分别计入。月书籍数按书去重。/ Chapters count distinct visited chapters per book/day, not completed chapters. Revisits on the same day deduplicate; monthly chapters sum daily counts, while monthly books deduplicate identities.
- 时长沿用现有票根计时准入：仅已加载的正文，工具面板、滑块、书架、锁屏和睡眠不计；超过五分钟无阅读活动后停止计时。单次采样间隔超过五秒或时间倒退不补算间隔，显示分钟向下取整。/ Time follows existing ticket eligibility: loaded body only, excluding tools, sliders, shelf, lock and sleep, and stopping after five inactive minutes. Gaps over five seconds and backward sampling time are excluded; displayed minutes round down.
- 跨午夜把有效短采样按日期拆分；跨月仅保留新月部分。进入新月清除内存中的旧月记录，详情不提供月份切换。日期跨月回拨也会切换月份并清空原月，不能恢复被清掉的历史。旧票根总量没有逐日逐书明细，不迁移为虚构记录。/ Midnight samples split by civil date, retaining only the new-month portion at month boundaries. A month change clears previous records without month navigation; rolling back across months clears rather than restores history. Old aggregate tickets cannot be migrated into invented daily book records.

## 持久化与边界 / Persistence And Limits

`reading_journal_model`管理纯日期、统计、章节位图、轮换及快照格式；`reading_journal`由UI任务独占，读取时钟、挂载内置卷并持久化。`ui_reading_journal`纯绘制，不读文件、分配内存或保存；页面通过`app_reading_journal_mode.h`请求进入详情，页面之间不直接引用实现。

The model owns pure date/statistics/chapter/scene/snapshot logic. The UI-thread service owns clock reads and internal-volume persistence; the renderer performs no IO, allocation or saves. Page transitions use the public app contract rather than including another page's implementation.

明细和插画状态保存在`/flash/reading-journal-0.bin`与`-1.bin`，不依赖TF卡，不上传网络，也不占用小容量NVS记录区。挂载失败不格式化。快照使用递增序列、小端字段与校验值交替写入，写入经`epd_flash_begin/end`保护；失败保留内存和上一有效槽。启动读取较新的完整快照，单槽损坏退回另一有效槽；两槽都无法读取时显示存储提示，不自动抹除。旧月备份槽在后续写入前可能仍含旧记录，不承诺物理擦除。

Details and scenes live in alternating internal-volume snapshots, independent of SD and networking, without filling the small NVS partition. Mount failure never formats. Sequence numbers, little-endian fields and checksums validate snapshots; EPD-guarded writes retain RAM and the previous valid slot on failure. Startup selects the newest valid slot and falls back if one is damaged; unreadable slots produce a notice without automatic erasure. An older slot can retain previous-month bytes until overwritten, not forensic erasure.

脏数据约每分钟尝试保存，锁屏、离页和手动换图立即尝试保存。内置卷暂不可用时保留本次内存会话，每30秒重试，恢复后合并磁盘和未保存记录；断电可能丢失最近未落盘的数据。容量为当月最多512条「书籍×日期」记录，每条最多8192个章节位；内存不足、容量上限或存储失败会显示提示，不伪装保存成功。标题最多127字节，按完整UTF-8边界截短，界面长标题另行省略。

Dirty data saves approximately once a minute, with immediate attempts on lock, exit or manual scene changes. Unavailable storage retains the current RAM session, retries every thirty seconds and merges it with disk after recovery. Power loss can lose unsaved changes. Limits are 512 book/day rows and 8192 chapter bits per row; allocation/capacity/storage failures show notices without claiming success. Titles are bounded to 127 bytes at UTF-8 boundaries and further elided for display.

首页布局仍纳入v33设置备份，但**手账明细与插画选择不纳入现有设置/书籍备份恢复**；恢复旧票根不会回填手账。固件版本仍沿用当前正式版，本地测试不创建OTA版本或更换发布附件。

Home placement remains in v33 settings backups, but **journal records and scene choices are excluded from existing settings/book backup/restore**. Restored tickets do not populate the journal. Local testing keeps the published version without creating OTA versions or replacing release assets.

## 素材与验证 / Assets And Verification

批准的105×81灰阶PNG与来源清单见[像素场景](../../../main/assets/reading-scenes/README.md)，以2倍整数显示，保持210×162区域。雨、落叶、瀑布和涟漪细节来自固定版本PixelFX的静态帧，保留MIT许可；新增主体与完整构图单独标识。固件无PixelFX运行依赖或动画，第三方权利不因本项目许可改变。

See the scene README for approved 105x81 grayscale PNGs and provenance. Integer 2x display preserves the 210x162 footprint. Static rain/leaves/waterfall/ripple details come from pinned PixelFX effects under MIT; new subjects and compositions are identified separately. Firmware has no PixelFX runtime or animation, and project licensing does not change third-party rights.

```sh
python3 tools/run_reader_baseline.py journal journal_reader journal_ui weather open controls settings_backup
python3 tools/dashboard_home_host_test.py
python3 tools/reading_origin_host_test.py
python3 tools/reading_journal_ui_host_test.py --output build/reading-journal-native
python3 tools/generate_reading_scenes.py --check
idf.py --preview build
```

模型/服务回归覆盖日历、去重、容量和分配失败、快照损坏与截断、重启、迟到挂载、午夜/月界及无效时钟；阅读接入执行真实计时函数；原生UI执行真实内置字体和4bpp绘制、详情手势/按键、书目分页、六图隔离及移除控件区域像素检查，并逐像素核对六图在上下组件槽、详情与空记录中的36种显示。导出图使用样例数据，不是设备已采集记录；宿主测试和S31构建不能替代实屏、实际写盘时延、突然断电和设备长期运行验收。

Tests cover the real model/service, timing integration, built-in font/4bpp drawing, detail actions/paging and isolated scene/control pixels, plus 36 exact source-pixel comparisons across all six scenes in both widget slots, details and empty journals. Exported pictures contain fixtures, not collected device history. Host tests and S31 compilation do not establish panel quality, real write latency, power-loss resilience or long-running device acceptance.
