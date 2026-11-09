# board_support

Metalio E-INK4 Plus（ESP32-S31）的板级支持：共享 I2C、epdiy 板定义、屏电源轨、TF 卡、触摸与加速度计装配，以及静默蜂鸣器接口。芯片驱动由独立组件提供。

Board support for Metalio E-INK4 Plus (ESP32-S31): shared I2C, the epdiy board definition,
screen rails, card storage, touch/accelerometer assembly and a silent buzzer interface.
Chip drivers live in separate components.

本地组件名为 `board_support`；上游来源为 `mindreset/read_pico`，保留其版权、许可和来源地址。
The local component is `board_support`, derived from `mindreset/read_pico`; retain its copyright, license and source URLs.

## 做什么 / What it does

- `board_init()`：初始化面板、探测 TF 卡、检查加速度计后使其掉电、启动 PMU 主机与触摸控制器。外设失败只清除对应的 `*_ready` 标志；仅面板失败视为致命。/ Brings up the panel, probes the card, checks the accelerometer and powers it back down, starts the PMU host and the touch controller. A failing peripheral only clears its `*_ready` flag; only the panel failing is fatal.
- `board_get_status()` / `board_get_ioe_status()`：TCA9555 引脚与电源轨状态快照，供出厂检查使用。/ TCA9555 pin and rail snapshots for factory checks.
- 电源轨：`board_rails_on()`、`board_toggle_vcomctl()`，以及 epdiy 板级钩子。/ Rails: `board_rails_on()`, `board_toggle_vcomctl()`, plus the epdiy board hooks.
- 加速度计轴映射：芯片坐标 → 设备坐标（屏朝上为 +Z）。/ Accelerometer axis mapping: chip → device coordinates (screen up is +Z).
- `board_sd_*`：非阻塞的探卡、挂载、格式化与容量查询。/ Non-blocking card probe, mount, format, capacity.
- `board_buzzer_tone()`：S31 使用 `board_buzzer_stub.c` 的静默实现。/ S31 selects the silent implementation in `board_buzzer_stub.c`.
- `board_metalio_e4p.c`：产品板级实现，直接驱动 TPS65185 和 TCA9555。旧 S3 板级、Zbit HPM 和蜂鸣器硬件实现已移除。/ Product board implementation for TPS65185 and TCA9555; legacy S3 board, Zbit HPM and hardware buzzer implementations have been removed.
- `buzzer_score.c` / `buzzer_duty_map.c`：仅供 `tools/buzzer_score_wav.c` 离线渲染，不参与产品构建；`buzzer_duty_map.h` 同时保留静默接口的参数类型。/ Offline rendering helpers for `tools/buzzer_score_wav.c`, outside the product build; `buzzer_duty_map.h` also retains the silent API's parameter type.

## 引脚 / Pins

通用配置入口仍为 `idf.py menuconfig` → *Read Pico board configuration*。`CONFIG_BOARD_*` 使用 `sdkconfig.rename` 迁移旧配置值；S31 实际引脚以 `board_metalio_e4p.c`、`board_sd.c` 与根 README 为准。

The generic menu remains `idf.py menuconfig` → *Read Pico board configuration*.
`sdkconfig.rename` migrates existing values to `CONFIG_BOARD_*`; actual S31 pins
are defined by `board_metalio_e4p.c`, `board_sd.c` and the root README.

## 依赖 / Dependencies

`board_i2c`、`epdiy`、`fca9555`、`cst836u`、`sc7a20h`、`device_power`、`e0470_epaper_waveform`。

`board_i2c`, `epdiy`, `fca9555`, `cst836u`, `sc7a20h`, `device_power`,
`e0470_epaper_waveform`.

## 原厂驱动契约 / Vendor driver contract

屏幕上电按原厂先等待 standby 和 PWRUP，再检查外部 PGOOD、ENABLE、VCOM 与芯片 PG；全部成功才开放 XOE。初次加三次完整掉电重试后仍失败会重启。VCOM setter 仅缓存，下一次完整上电按 VCOM2→VCOM1 写入；刷新收尾仍只撤驱动脚，真正关电才撤 VCOM。
Panel power follows vendor standby/PWRUP delays, external PGOOD, ENABLE, VCOM and chip PG checks; XOE opens only on complete success. Four failed full power-cycle attempts trigger a restart. VCOM setters cache requests for VCOM2→VCOM1 writes on the next full power-on. Scan completion releases drive pins; VCOM drops on real power-off.

SC7A20H 和 TCA9555 共用 GPIO2，采用原厂开漏低有效拾起配置和上拉。睡眠前须稳定为高，醒后先消费按键和加速度计源寄存器，再处理共享线；拾起配置失败时静默加速度计并回退低有效按键唤醒。
SC7A20H and TCA9555 share GPIO2 with vendor open-drain active-low pickup and a pull-up. The line must settle high before sleep. Wake checks consume key and accelerometer source events before shared-line handling; failed pickup setup silences the sensor and falls back to active-low key wake.

总线、芯片原厂来源和适配边界见 [board_i2c](../board_i2c/README.md)。FT6336U 的触点编号、盖板键范围和模式初始化由 `cst836u` 兼容接口处理。
See board_i2c for vendor provenance and adaptation boundaries. The cst836u compatibility API handles FT6336U tracking IDs, cover-key bounds and mode startup.

## 许可 / License

原有部分为 Apache-2.0，见 [LICENSE](LICENSE)；移植原厂部分同时保留 [MIT 声明](../board_i2c/LICENSE.Metalio)。

Existing portions use Apache-2.0; adapted vendor material retains its MIT notice linked above.
