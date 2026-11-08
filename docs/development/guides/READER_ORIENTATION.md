# 阅读方向 / Reader Orientation

功能 ID：`F-ORIENTATION`。2026-11-08 起的本地实现；入口以 `app_book` 与顶部快捷栏为准。
Feature ID: `F-ORIENTATION`. Local implementation from 2026-11-08, entered through `app_book` and the top quick layer.

## 使用 / Use

- 阅读 TXT、EPUB 或受支持的传统 MOBI 时，从当前画面顶部下拉快捷栏。第二排左侧的旋转锁定圆钮控制自动旋转，图标为环形箭头围住锁头：灰底开锁表示自动旋转，白底闭锁表示锁定。竖屏第一排保持原来的四枚圆钮；前光滑杆在第二排下方展开。横屏采用左右分区：左侧四键紧凑排在首行，旋转锁定留在第二行；右侧亮度、色温滑杆分别对齐这两行。横屏阅读快捷层高度为404px，展开前光不再向下叠加面板。
  While reading TXT, EPUB or supported traditional MOBI, pull down from the current screen's top edge. The rotation-lock circle at the left of row two uses a circular arrow around a lock: gray with an open lock means automatic rotation, white with a closed lock means locked. In portrait, keep the four original circles in row one and expand frontlight sliders below row two. Landscape uses two columns: four compact keys on the first left row, rotation lock on the second, and brightness/temperature tracks aligned with those rows on the right. The landscape reader layer stays 404px tall when frontlight controls expand.
- 默认锁定正常竖屏。点一次开启重力自动旋转，收起快捷栏后把设备立起并转向；连续稳定约半秒后，在正竖屏、倒竖屏和两个横屏之间切换。再次点按锁定当前显示方向，后续转动设备保持不变。
  Each opening starts locked in normal portrait. Tap once to enable gravity rotation, dismiss the quick layer and turn the upright device. About half a second of stability selects normal/inverted portrait or either landscape. Tap again to lock the displayed orientation.
- 平放、持触、拖选、字典、快捷栏和其他弹层期间保持当前正文方向；抬手后还有600ms静默期。自动模式与锁定方向只属于当前开书会话，不写入设置或配置备份。
  Flat placement, held touch, selection, dictionary, quick controls and other overlays suspend body rotation; release adds a 600ms quiet window. Mode and locked direction belong only to the open-book session, without settings or backup persistence.
- 正文、页眉页脚、进度线、章节题头和底部工具栏跟随方向。目录、阅读设置、系统菜单、电源弹层和锁屏暂用正常竖屏；回正文时恢复会话方向。关书、返回书架或离开阅读应用后恢复正常竖屏，下次开书重新关闭自动模式。
  Body, chrome, progress, headings and the bottom toolbar follow orientation. TOC, reader settings, system menus, power dialogs and lock screens temporarily use normal portrait, restoring the session direction on return. Closing the book, returning to the shelf or leaving the app restores normal portrait; the next opening starts with automatic mode off.

屏幕方向与「横排/竖排文字」独立。翻页分区与手势按当前画面解释，三枚屏外触摸键保留原来的实体位置和动作映射。
Screen orientation is independent of horizontal/vertical typesetting. Tap regions and gestures follow the visible frame; the three off-panel cover keys keep their physical positions and action mappings.

## 所有权与恢复 / Ownership And Recovery

`display_orientation` 只封装 EPD 方向编号和竖屏触点转换，不读传感器、不决定页面行为。正常竖屏对应 `EPD_ROT_INVERTED_PORTRAIT`；UI 任务在触点摄取前转换屏内坐标，采样队列保留原始竖屏帧，屏外键与诊断字节不转换。
`display_orientation` wraps EPD numbering and native portrait-contact conversion without sensor reads or page policy. Normal portrait is `EPD_ROT_INVERTED_PORTRAIT`. The UI task converts on-panel contacts before ingestion, while queues retain native portrait frames and cover keys/raw diagnostic bytes remain untouched.

`reader_orientation` 仅判断稳定方向。页面与晃动翻页共用25Hz加速度计配置、50ms轮询及现有供电交还；使用 `board_accel_read` 已转换的设备轴，不新增后台任务。主轴至少650mg、比另一平面轴大200mg，合加速度在0.7–1.3g内且Z轴不超过800mg；样本间隔不得超过200ms，任一轴跳变超过160mg重新计时。候选换向期间不触发晃动翻页，已有晃动方向按当前屏轴转换。
`reader_orientation` only detects stable direction. The page shares the existing 25Hz configuration, 50ms polling and sensor handoff with shake turns, using device axes from `board_accel_read` without another task. A dominant axis must exceed 650mg and the other in-plane axis by 200mg, with magnitude 0.7–1.3g and Z at most 800mg. Gaps above 200ms or per-axis jumps above 160mg restart stability. Candidate turns suppress shake paging; shake directions map to the current screen axes.

页面先等待预绘交还，在绘制锁内更新方向、章节题头和正文几何，再按原文字字节锚点重建当前章节布局。页码可变，锚点不因系统竖屏往返累积漂移；图片通栏宽度、字格命中和预绘缓存同时失效。新方向排版失败时恢复旧方向/布局；旧布局也无法恢复时沿既有关书路径回书架。系统竖屏重排失败时同样清理到竖屏，避免系统界面使用横屏触点。失败后自动换向退避2秒。
The page joins prefetch, updates orientation/heading/body geometry under the draw lock and rebuilds the current chapter around its source-byte anchor. Page numbers may change without anchor drift on system-portrait round trips. Image bleed, glyph hits and prefetch caches invalidate together. A failed new layout restores the old orientation/layout; failure to restore closes through the existing shelf fallback. Failed system-portrait reflow also closes in portrait so system controls retain valid touch geometry. Automatic retries back off for two seconds.

`render()` 仍只绘制；方向切换位于输入、tick、呈现准备和生命周期边界。页面通过 `app_desc_t` 可选回调提供快捷钮状态和系统竖屏准备；其他页面不依赖阅读模块。
`render()` remains paint-only. Orientation changes occur at input, tick, presentation preparation and lifecycle boundaries. Optional `app_desc_t` callbacks supply quick-button state and system-portrait preparation without making other pages depend on reading modules.

## 验证入口 / Verification

```sh
python3 tools/run_reader_baseline.py orientation controls input quick quick_pixels loop heading journal_reader
python3 tools/run_reader_baseline.py dictionary
```

字典回归需按 `tools/dictionary-requirements.txt` 安装其已有构建依赖。`orientation` 使用真实方向策略、触点转换、阅读事务、会话和分页器，覆盖四向全屏触点、字节锚点、窗口/全屏、横竖排、系统往返、锁定与失败恢复。`controls` 同时覆盖传感器共用、静默期和晃动轴；快捷层覆盖第二排绘制、按压、命中边界、横屏左右分区、滑杆两端/拖动/临时展开与四向像素边界。
Dictionary regressions require the existing dependencies in `tools/dictionary-requirements.txt`. `orientation` exercises real policy, touch conversion, reader transactions, session and pagination across all screen contacts, anchors, window/fullscreen, writing modes, system round trips, locking and failure recovery. `controls` also checks shared sampling, quiet periods and shake axes; quick-layer suites cover row-two painting, feedback, hit bounds, landscape columns, slider endpoints/drag/session expansion and pixel bounds in four orientations.

宿主与S31构建不代表真机验收。设备上仍需确认四边重力符号、接近水平时的稳定感、四向触摸/实体键、插图/背景、字典、锁屏和退出恢复，以及旋转重排时延、内存与残影；这些检查需要单独授权刷机。
Host checks and S31 compilation do not establish physical acceptance. Device checks remain for gravity signs, near-flat stability, four-way touch/cover keys, images/backgrounds, dictionary, lock/exit restoration, reflow timing, memory and ghosting; flashing requires separate authorization.
