# board_i2c

Metalio E-INK4 Plus（ESP32-S31）的共享总线事务。驱动持有设备句柄；此组件提供可嵌套的任务锁、100 kHz 速率、10,000 µs SCL 拉伸窗口，以及原厂的 STOP 分段/失败回退读取。不从 ISR 调用，驱动销毁前必须由调用方停止其消费者。
Shared bus transactions for Metalio E-INK4 Plus (ESP32-S31). Drivers own device handles. This component supplies a recursive task lock, 100 kHz rate, 10,000 us SCL stretching window, vendor STOP reads and failed-read fallback. Never call from an ISR; stop consumers before destroying their driver.

## 原厂来源 / Vendor source

参考 [CloudZao/Metalio-E-INK4-Plus](https://github.com/CloudZao/Metalio-E-INK4-Plus)，固定提交 `2f55cb1234baae4968982feb23e7a9001dac16f0`（v2.0.69版本）。完整 MIT 声明见 [LICENSE.Metalio](LICENSE.Metalio)，适用于移植的原厂内容；已有组件的 Apache 声明继续保留。
Adapted from the pinned vendor revision above. The complete MIT notice is retained in LICENSE.Metalio for adapted vendor material; existing component Apache notices remain in place.

| 原厂文件 / Vendor path | 本地职责 / Local responsibility |
| --- | --- |
| `main/boards/metalio-e-ink4-plus/bus/metalio_i2c1.c` | 共享事务锁与总线参数 / Shared lock and bus configuration |
| `main/boards/common/power/power_i2c.c` | combined 失败后延时并用 STOP 重试 / Delayed STOP fallback after combined-read failure |
| `main/boards/metalio-e-ink4-plus/bus/pca9555.c` | `fca9555` 中的 TCA9555 单寄存器读取 / TCA9555 individual register reads |
| `main/boards/metalio-e-ink4-plus/display/epd_board_metalio_eink4_plus.c` | `board_support` 的 TPS65185 上电、VCOM、重试 / TPS65185 power, VCOM and retries |
| `main/boards/common/sensor/sc7a20h.cc` | `sc7a20h` 的共线静默与拾起配置 / Shared-IRQ silence and pickup configuration |
| `main/boards/metalio-e-ink4-plus/display/metalio_touch.c`、`main/display/lv_adapter_epdiy_touch.cc` | `cst836u` 中 FT6336U 复位、模式、坐标和盖板键 / FT6336U reset, mode, coordinates and cover keys |
| `main/boards/common/power/cx25601n.cc`、`main/boards/common/sensor/pcf8563.cc` | `device_power` 中充电策略与 RTC 初始化 / Charging policy and RTC startup |

## 适配边界 / Adaptation boundary

- 原厂 C++ 对象和板级设备缓存改为本项目已有 C 句柄；任务锁保护完整分段事务，充电寄存器读改写也在同一锁中。
  Vendor C++ objects and device caches use existing C handles here; a task lock covers complete segmented transactions and charger register read-modify-write.
- `fca9555_*`、`cst836u_*` 是现有接口名称，S31 实际芯片分别为 TCA9555、FT6336U。上层手势、触摸队列、电量滤波、文件交还与协议格式沿用产品契约。
  Existing API names wrap TCA9555 and FT6336U on S31. Upper-layer gestures, touch queues, battery filtering, file handoff and wire formats retain their product contracts.
- 充电策略使用现有电源轮询的非阻塞步骤；200 ms 是最早恢复充电时间，实际执行受下一次轮询调度影响。活动页、锁屏按键检查都会轮询；ESP 深睡或断电后服务停止。它不是原厂独立任务的逐字复制。
  Charging policy runs as nonblocking steps in existing power polling. The 200 ms recharge recovery time is a lower bound; execution depends on the next poll. Active pages and lock-key checks poll; ESP deep sleep or power-off stops the service. This differs from the vendor's independent task scheduling.
- RTC 寄存器统一存 UTC（2000–2099），显示时区由应用处理；读取不依赖进程 TZ。启动清 STOP 和 AF/TF；电压失效标志、非法 BCD 或公历日期不用于设置系统时钟。
  RTC registers hold UTC for 2000–2099; the application handles display time zones. Reads are independent of process TZ. Startup clears STOP and AF/TF; voltage-loss flags, invalid BCD or calendar dates cannot set the system clock.

## 验证 / Validation

```sh
python3 tools/run_reader_baseline.py metalio_drivers board battery battery_filter storage
```

测试执行真实总线、芯片驱动及睡眠函数，包含寄存器对翻转、事务并发、故障注入和共享低有效中断。宿主模型与 S31 编译不能代替真机充电、电压、拾起和显示时序验证。
Tests execute real bus/chip drivers and sleep functions with register-pair toggling, concurrent transactions, injected faults and shared active-low IRQs. Host models and S31 builds do not replace device charging, voltage, pickup and display-timing validation.
