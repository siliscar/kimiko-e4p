# fca9555

本项目保留 `fca9555_*` 接口，S31 实际驱动 TCA9555（地址 `0x20`、共用中断 GPIO2）；板级引脚和方向由 `board_support` 配置。
The project retains the `fca9555_*` API for the S31 TCA9555 at `0x20`, with shared interrupt GPIO2. `board_support` owns board pin and direction configuration.

- 采用原厂 100 kHz 与 STOP 分段读取；总线锁覆盖指针写和后续读，避免其他任务改写指针。
  Vendor 100 kHz and STOP-separated reads; a bus lock spans pointer writes and reads to prevent interleaving.
- TCA9555 连续读只在寄存器对内翻转。`read_n`、`read_all`、`read_io` 逐寄存器读取，失败时不提交部分结果；成功的完整快照才更新 CFG/INV 缓存。
  TCA9555 sequential reads toggle within a register pair. The multi-register APIs address each register and leave outputs unchanged on failure; only successful full snapshots refresh CFG/INV caches.
- INT# 开漏低有效，各口读取自己的 Input 才能清除对应中断；`clear_int` 即使第一口读失败也尝试第二口。
  INT# is open-drain active-low. Each port clears through its own Input read; `clear_int` attempts the second port even if the first fails.
- `fca9555_init` / `fca9555_deinit` 管理设备句柄，总线由调用方持有；`scl_speed_hz` 字段仅保留接口兼容，实际固定 100 kHz。
  Init/deinit own device handles while the caller owns the bus. The retained speed field is compatibility-only; the actual rate is fixed at 100 kHz.

公开接口见 [include/fca9555.h](include/fca9555.h)，原厂来源、版本与 MIT 声明见 [board_i2c](../board_i2c/README.md)。已有 Apache-2.0 声明保留。
See the public header for APIs and board_i2c for pinned vendor source and MIT notice. Existing Apache-2.0 notices remain.
