# device_power

设备电源接口。S31 产品编译 `device_power_metalio.c`，适配 BQ27220 电量、CX25601N 充电、PCF8563 RTC 与 TCA9555 电源键。旧板 CW32 主机后端已移除。

Device power interfaces. The S31 product compiles `device_power_metalio.c` for BQ27220
battery readings, CX25601N charging, PCF8563 RTC and TCA9555 power keys. The legacy
CW32 host backend has been removed.

本地组件名为 `device_power`；上游来源为 `mindreset/read_pico_pmu`，保留其版权、许可和来源地址。
The local component is `device_power`, derived from `mindreset/read_pico_pmu`; retain its copyright, license and source URLs.

## 做什么 / What it does

- `device_power_refresh()` / `device_power_poll()`：轮询当前板的电源键、电量和充电状态；前者复用后者。/ Poll current-board power keys, battery and charging state; refresh delegates to poll.
- `device_power_get()`：提供带有效标记的兼容快照；电池电压读错或越界时清除有效位，调用方不能沿用旧电量。/ Return the compatibility snapshot with validity flags; invalid or failed voltage samples clear validity rather than presenting stale battery percentages.
- 电量：电压/电流负载补偿、EMA、曲线映射与单向限速，保留已有 NVS 电量恢复及按需校准日志。/ Battery: load compensation, EMA, curve mapping and one-direction rate limiting, retaining saved-percent recovery and optional calibration logging.
- `device_power_cmd()`：支持 RTC 读时与同步；未实现的旧命令返回 `ESP_ERR_NOT_SUPPORTED`。/ Supports RTC reads and sync; unimplemented legacy commands return `ESP_ERR_NOT_SUPPORTED`.
- 电源键：TCA9555 P17 采样，去抖后提供短按、长按和唤醒消费接口。/ Samples TCA9555 P17 and exposes debounced short/long press and wake-consumption APIs.
- 电源交接：`device_power_report_sleep()` 返回不支持，由应用按当前板路径转入关机；`device_power_power_off()` 发送关机脉冲，耗尽重试后进入 ESP 深睡。/ Soft sleep reports not supported so the application escalates along the current board's power-off path; power-off sends the pulse train and falls back to ESP deep sleep after its retry limit.
- 面板 VCOM：启动读取已有标定值，写入接口仅供出厂流程使用。/ Reads existing calibration at boot; the write API is reserved for factory setup.

## 电量显示 / Battery display

电压补偿采用 BQ27220 的有符号电流：正向充电减压、负向放电加压，内阻按 180 mΩ、补偿封顶 150 mV。CX 状态仍决定充电标志，但不决定补偿方向，避免插拔时两个芯片读数暂时不同步导致反向补偿。日志 `comp` 与实际滤波共用同一换算。
Compensate voltage using signed BQ27220 current: subtract while charging and add while discharging, using 180 milliohms capped at 150 mV. CX still selects the charging flag but not the compensation sign, avoiding reversed compensation when chip readings temporarily disagree during cable changes. Logged `comp` uses the same conversion as the filter.

普通区间每 60 秒最多变化 1%，充放电切换后重新计时；确认充满仍直接对齐 100%。放电目标不高于 14% 时保留每 10 秒最多下降 1%，空电目标允许清掉不足 1% 的尾数，不让死区长期显示 1%。这是显示平滑，不是延长实际续航。
Normal levels move at most 1% every 60 seconds, rearming after charging transitions; confirmed full still aligns directly to 100%. Discharge targets at or below 14% retain 1% steps every ten seconds, and an empty target can clear a sub-1% remainder. Smoothing does not extend physical runtime.

中段 4.05/4.00/3.95 V 目标为 73.3/69.5/62.9%，来自连续清醒采样的相对放电量，以既有 4.10 V / 79% 和假定 2050 mAh 为基准。它们不是绝对电量实测；锁屏空白、校时跳变和芯片 SOC 不参与容量真值拟合。3.95–3.60 V 暂用直线衔接，3.60 V 及以下沿用原锚点；完整低段与实际可用容量仍须完整充放电验证。
Targets at 4.05/4.00/3.95 V are 73.3/69.5/62.9%, derived from relative discharge over continuous awake samples, anchored to the existing 4.10 V / 79% and assumed 2050 mAh capacity. They are not measured absolute SOC. Sleep gaps, wall-clock jumps and gauge SOC are not calibration ground truth. The 3.95–3.60 V range provisionally interpolates to unchanged anchors at/below 3.60 V; the full low range and usable capacity still require a complete charge/discharge validation.

高压近零电流判满需要连续有效采样；超过 5 秒的采样空白、读数失败或明显放电会重新确认，保留硬件 top-off 和原电压门槛。显示算法不修改充电电流、截止电压、终止策略或欠压保护。
High-voltage near-zero-current full confirmation requires continuous valid sampling. Gaps longer than five seconds, read failures or material discharge restart confirmation, retaining hardware top-off and existing voltage gates. Display logic does not change charge current, regulation voltage, termination policy or undervoltage protection.

NVS 百分比带 `soc_ver` 算法版本；旧的无版本记录和不兼容记录不恢复，首次按新曲线估算，不清空其他设置。以后在五分钟周期、充电状态变化、累计变化至少 2%（至少隔一分钟）及正常重启/关机前保存有效值，相同值不重复写。保存失败不记作成功；意外断电仍可能丢失最近变化，启动时仍按电压容差决定是否恢复。
NVS percentages carry an algorithm revision in `soc_ver`. Unversioned or incompatible records are ignored so the first start estimates from the new curve without erasing other settings. Save valid values every five minutes, on charger changes, after at least 2% movement (at least one minute apart), and before normal restart/shutdown; duplicate values skip writes. Failed saves are not cached as successes. Unexpected power loss may still lose recent movement, and boot restoration remains subject to voltage tolerance.

## 电源键采样 / Power-key sampling

主循环启动默认暂停的快采任务，正常页面启用后每轮只读 P17，轮间等待 10 ms；原有 20 ms 按下复查、30 ms 短按门限与 1 s 长按门限保持不变。事件与直读共用互斥锁及原有短按/长按待处理标记，长按优先；快采不执行电量采样、文件写入或绘屏。主循环恢复处理前保留待处理动作，不承诺在硬件推屏中途立即锁屏。
The loop starts an initially paused task which reads only P17 on active pages and waits 10 ms between polls. Existing 20 ms press confirmation, 30 ms short-press and 1 s long-press thresholds remain. Sampling and direct reads share a mutex and the existing short/long pending flags, with long presses taking priority. The task does no battery, file or display work. Pending actions survive until the loop can handle them; locking need not interrupt a hardware refresh already in progress.

`device_power_key_sampler_enable(false)` 等待在途读取结束，丢弃旧动作及当前按住状态的释放；浅睡、下电和 USB/设备独占前必须完成此交接，避免 TCA Input 读操作清掉唤醒中断。嵌套调用按返回的原状态恢复。锁屏保留直读唤醒，解锁先消费唤醒键再恢复快采；任务创建失败保留原轮询，反初始化暂停任务，后续初始化可复用。页面导航暂停快采，下一轮按新页面归属恢复。
`device_power_key_sampler_enable(false)` joins in-flight reads and discards stale actions and the current held press's release. Complete this handoff before sleep, power-off or USB/device-exclusive use so TCA Input reads cannot clear wake interrupts. Nested callers restore the returned previous state. Locks retain direct wake reads and consume the wake press before resuming sampling. Failed task creation retains direct polling; deinit pauses the reusable task. Navigation pauses sampling until the next iteration establishes the new page's ownership.

浅睡专用的 `device_power_take_key_wakeup()` 每次仍读取电源键，但可复用采样起点距今不足 10 ms、且电压/电流/现有充电芯片状态全部有效的电量样本，减少同一轮等待边界内的重复总线访问。样本过期、读取失败、时钟回退或充电维护/恢复已经到期时立即重新采样。活动页的 `device_power_poll()` 不复用；2 秒浅睡定时兜底、充电寄存器策略和欠压阈值不变。减少访问次数不是已测得的节电比例。

The sleep-only `device_power_take_key_wakeup()` still reads POWER on every call, but may reuse a battery sample whose start is less than 10 ms old with valid voltage, current and status from any present charger. This avoids duplicate bus access at the same wait boundary. Expiry, read failure, clock rollback or due charger maintenance/resume forces a fresh sample. Active-page `device_power_poll()` never reuses samples; the two-second sleep timer, charger register policy and undervoltage threshold remain unchanged. Fewer accesses are not a measured power-saving percentage.

## 主机浅睡诊断 / Host light-sleep diagnostics

主机等待入口在 `main/power/sleep.c`。排查构建可开启 `CONFIG_APP_SLEEP_DIAGNOSTICS`（默认关闭）：每次等待只在 RAM 累计，退出并恢复外设后，向 NVS `sleep_diag/last` 覆盖保存一份汇总，并输出 `SLEEP_SUMMARY origin=live`。不逐切片写 Flash/TF，不新增唤醒定时器，定时切片仍为 2 秒；保存失败仅记录 `save_error`，不阻止恢复。

The host wait entry lives in `main/power/sleep.c`. Investigation builds can enable `CONFIG_APP_SLEEP_DIAGNOSTICS` (off by default): each wait accumulates in RAM, then overwrites one NVS `sleep_diag/last` summary after leaving the wait and restoring peripherals, logging `SLEEP_SUMMARY origin=live`. There are no per-slice flash/card writes or extra wake timers; slices remain two seconds. Save failures only set `save_error` and never block recovery.

同一诊断构建启动时读取记录，校验格式后输出 `origin=saved`。这是上一份成功保存的等待记录，**不是本次启动已完成浅睡的证据**；记录的 `elf` 是采集固件 ELF SHA-256 前缀（最多 16 位，长度受 SDK 配置影响），`unix_s` 依赖设备校时，`start_us` 是当次启动的单调时钟。保存失败时旧记录仍可能存在。无记录、读取失败或格式不符时不伪造记录，不清空原 NVS。

The same diagnostic build reads and validates the record at boot, logging `origin=saved`. This is the last successfully saved wait, **not evidence that the current boot has already slept**. `elf` identifies the collecting firmware with an ELF SHA-256 prefix (up to 16 hexadecimal characters, depending on SDK configuration); `unix_s` depends on device clock synchronization, and `start_us` belongs to that boot's monotonic clock. A failed save can leave an older record. Missing, unreadable or invalid records are never fabricated or used to erase NVS.

| 字段 / Fields | 含义 / Meaning |
| --- | --- |
| `attempts`, `ok`, `failed` | 实际浅睡调用次数及返回结果；资源交还失败不计为已睡。/ Actual sleep calls and results; rejected resource handoff is not sleep. |
| `timer`, `gpio`, `other` | 仅统计成功调用的唤醒原因；混合原因分别计数，和可能大于 `ok`。/ Causes from successful calls only; combined causes count separately, so the sum can exceed `ok`. |
| `lt80ms`, `min_call_us`, `max_call_us` | 成功调用的短返回次数和时长范围；电源键及时唤醒也可能短于 80 ms，并非都属异常。/ Short successful returns and duration bounds; prompt key wakes can legitimately be below 80 ms. |
| `ok_call_us`, `rejected_call_us` | SDK 调用的累计停留时间，包含进出开销，不是精确硬件睡眠时间或电流测量。/ Accumulated SDK call residence including entry/exit overhead, not exact hardware sleep time or current. |
| `total_us`, `outside_call_us` | 总等待入口耗时及调用外耗时，含前置准备、轮询与恢复；不含最终保存/汇总日志，不等同于单次唤醒开销。/ Total entry duration and time outside calls, including preparation, polling and recovery; excludes final save/summary logging and is not per-wake overhead. |
| `ble_blocked`, `timer_errors`, `irq_low` | 蓝牙未停栈、定时器配置失败、共享中断清除后仍低的轮询次数。/ Bluetooth teardown block, timer setup failures and polls with shared IRQ still low after clearing. |
| `key_latched`, `key_read_errors`, `unresolved_gpio` | 唤醒后保全的 P17 低、输入读取失败，以及未判明为按键或有效拿起的 GPIO 返回。/ Captured post-wake P17 lows, input read failures and GPIO returns not identified as a key or accepted pickup. |
| `exit` | 0 按键，1 拿起，2 文件任务未归还，3 共享中断持续低，4 定时器连续失败，5 浅睡连续失败。恢复活动页不表示真的按了电源键。/ 0 key, 1 pickup, 2 file jobs busy, 3 stuck IRQ, 4 repeated timer failures, 5 repeated sleep failures. Recovery exits do not prove a physical key press. |

成功 GPIO 唤醒后，先保全实际读到的 TCA P17 低电平，再沿用原按键复查及拿起判断，避免已读到按下却因复查期间松手而丢失；睡眠失败不使用陈旧的唤醒原因，其他共享中断不直接变成电源键。如果按键在第一次读取前已经释放，仍可能无法判明来源，本轮没有消除这个硬件信息缺口，因此不延长定时切片、不改变 IO 方向或拿起设置。

After a successful GPIO wake, preserve an actually observed TCA P17 low before the existing key recheck and pickup decision, avoiding loss when release occurs during rechecking. Rejected sleep never consumes stale wake causes, and unrelated shared interrupts never become keys solely from the GPIO cause. A key released before the first read can still be unidentifiable; this hardware-information gap remains, so this change does not lengthen slices or alter IO directions or pickup settings.

仅有 USB 数据线时，可先连线采集短期行为日志，再拔线等待、主动按电源键唤醒后重连；如果重连丢失串口输出，可在连接稳定后重新启动并采集 `origin=saved`。重连本身不会自动补发，保存尚未完成就复位仍可能丢本次记录。USB 供电/充电与纯电池条件分开记录；串口掉线不能证明低功耗，板载电量计或等待时长也不能证明节电百分比。诊断关闭后不读写这份记录、不输出汇总；恢复正式构建时应关闭本开关及独立的逐次 `KIIKO_SLEEP_DIAG`。

With only a USB cable, first capture short-term connected behavior, then disconnect, wait, wake with POWER and reconnect. If serial output is lost, restart after the connection is stable and capture `origin=saved`. Reconnection alone does not replay it, and a reset before saving finishes can lose the current record. Label USB-powered/charging and battery-only conditions separately: serial loss, gauge readings and wait durations do not establish a power-saving percentage. With diagnostics off, this record is neither read nor written and no summary is emitted; disable this option and the independent per-return `KIIKO_SLEEP_DIAG` for production builds.

## 充电与 RTC / Charging and RTC

CX25601N 请求 1 A，经原厂 80 mA 档位量化为 960 mA，输入限流跟随量化值；VREG 为 4.4 V。轮询按有效电压切换终止，输入存在且空闲/近零电流时补充充电，无输入且有效电压 ≤3.1 V 时按原厂关 BATFET。配置不完整时报告失败，并尽力恢复硬件终止与停充。
CX25601N quantizes the 1 A request to 960 mA using vendor 80 mA steps; the input limit follows that value and VREG is 4.4 V. Polling controls termination from valid voltage, kicks recharge on input-present idle/near-zero-current status and shuts BATFET on valid no-input voltage at or below 3.1 V. Incomplete configuration reports failure and attempts to restore hardware termination and disable charging.

维护使用现有轮询上的非阻塞状态机，恢复充电最早在 200 ms 后的下一次轮询执行，失败继续重试；不更改电量曲线、补偿与滤波。PCF8563 启动清 STOP 和 AF/TF，寄存器存 UTC，非法 BCD、日期和电压失效标志不用于系统校时。
Maintenance uses a nonblocking state machine on existing polling, resuming recharge on the next poll at least 200 ms later and retrying failed restoration. Battery curves, compensation and filtering remain unchanged. PCF8563 startup clears STOP and AF/TF, stores UTC and rejects invalid BCD/calendar dates or voltage-loss flags for system clock updates.

关机与反初始化前恢复硬件充电终止，并完成待恢复的充电使能；失败则交还调用方，不带着临时停充状态断电。
Before power-off or deinit, restore hardware termination and pending charge enable; failure returns to the caller instead of powering off with a temporary charge disable.

原厂来源与调度差异见 [board_i2c](../board_i2c/README.md)。
See board_i2c for vendor provenance and scheduling differences.

## 电量校准日志 / Battery calibration log

设置中的「电量校准日志」默认关闭。开启后，清醒状态每分钟记录新采样，采样有效性、CX 状态或判满结果变化时额外记录；锁屏等待开始和结束、关机与重启请求也产生事件。关闭时补一条会话结束记录，之后不再采样芯片 SOC 或写新日志。写卡在独立任务完成，不增加睡眠唤醒定时器。
The battery-calibration log defaults off. While enabled, new awake samples are recorded each minute, with extra records for sample-validity, CX-status and full-decision changes, locked-wait boundaries, and power-off/restart requests. Disabling adds a session-end record and stops further gauge-SOC reads and new logging. A separate task writes the file without adding sleep wakeup timers.

串口 `batt-meta` 与卡上 `# battlog` 元信息包含格式版本、固件版本、ELF SHA-256、编译时间、启动编号、复位原因、曲线散列和算法参数。同一正式版本的本地测试包通过 ELF 散列区分；每次重新开启日志递增 `session`，本次开机内的记录序号 `seq` 持续递增。先按 `boot/session` 分段，再按 `seq` 分析；不能只按固件版本合并数据。
Serial `batt-meta` and card `# battlog` metadata include format and firmware versions, ELF SHA-256, build time, boot ID, reset reason, curve hash and algorithm parameters. ELF hashes distinguish local test images sharing a published version. Each enable increments `session`, while `seq` increases throughout the boot. Group by `boot/session` before analyzing sequences; firmware version alone is insufficient.

格式 2 的每条数据均带 `fmt=2`，主要字段如下。旧格式记录可与新格式共存，但必须分别解析；未知值为 `-1`，有符号电流的 `-1 mA` 本身也是合法读数，必须结合 `valid` 判断。
Each format-2 row carries `fmt=2`. Legacy and new rows may coexist and require separate parsers. Unknown values are `-1`; signed current of `-1 mA` is also a valid measurement, so always consult `valid`.

| 字段 / Field | 含义 / Meaning |
| --- | --- |
| `t`, `up_ms` | 事件的日历秒和本次开机单调毫秒；未校时 `t=-1`。/ Event wall seconds and boot-monotonic milliseconds; unknown wall time is -1. |
| `sample_t`, `sample_ms`, `read_us` | 最近采样尝试的日历时间、单调时间与电压/电流/CX 读取耗时；事件不会伪造新采样。/ Latest sampling-attempt times and voltage/current/CX read duration; events never fabricate new samples. |
| `write_ms`, `age_ms` | 输出开始的单调时间及最近采样的年龄，含队列延迟；不代表落盘完成时间。/ Output-start monotonic time and sample age including queue delay, not completed persistence. |
| `valid` | 位 1 电压、2 电流、4 CX、8 芯片 SOC；无效电压不会沿用旧值，无效电流不会写成零。/ Bits 1 voltage, 2 current, 4 CX, 8 gauge SOC; invalid voltage never reuses an old value and invalid current never becomes zero. |
| `raw`, `ma`, `cx`, `chg` | 原始 mV、正充负放 mA、完整 CX 状态字节、现有算法的充电判断。CX 低三位保留输入状态编码。/ Raw mV, signed mA, complete CX status byte and the existing charging decision; CX low three bits retain the input-state code. |
| `done`, `done_src` | 现有算法判满；来源 0 无、1 CX top-off、2 软件高压低电流持续判定。不是独立满电测量。/ Existing full decision; source 0 none, 1 CX top-off, 2 sustained software voltage/current decision, not independent proof of full capacity. |
| `comp`, `ocv`, `curve`, `disp` | 即时负载补偿 mV、EMA 后 mV、查表目标和最终显示千分比；`ocv` 不是静置实测开路电压。/ Instant compensated mV, EMA mV, curve target and displayed permille; `ocv` is not measured rested open-circuit voltage. |
| `ic_soc`, `ic_age_ms` | 每分钟独立读取的芯片百分比和其输出时年龄，仅用于对照；读取失败为 -1。/ Separately sampled gauge percent and its age at output, comparison only; -1 after failed reads. |
| `locked`, `lock_ms` | 是否处于锁屏等待及会话内累计等待时长，包含定时唤醒轮询，不是精确 CPU 睡眠时长。/ Locked-wait state and session wait duration including timer-wake polling, not exact CPU sleep duration. |
| `covered_ms`, `unknown_ms` | 会话内可积分与无覆盖的时间。锁屏等待、无效端点、超过 5 秒的采样间隔及事件边界均不外推容量。/ Eligible and uncovered session durations; never extrapolate locked waits, invalid endpoints, intervals above 5 seconds or event boundaries. |
| `in_uah_est`, `out_uah_est` | 相邻有效清醒采样按时间作梯形积分，跨零分别计算充入/放出，单位 µAh；均为估算。/ Separate charge/discharge trapezoidal estimates in µAh from adjacent valid awake samples, splitting zero crossings. |
| `dropped`, `io_errors`, `file_missed` | 队列丢弃、文件/格式失败、未确认成功写入文件的累计记录数；同步或关闭失败时数据可能已写入，I/O 计数在后续记录反映。/ Queue drops, file/format failures and records without confirmed successful file persistence; data may already have been written when sync or close fails, and I/O counters appear in subsequent rows. |

`event` 为可组合位图：1 定期/首次采样、2 开启、4 关闭、8 充电/CX 变化、16 判满变化、32 有效性变化、64 清醒长空白、128 锁屏等待开始、256 等待结束、512 关机请求、1024 重启请求、2048 欠压保护请求。事件只说明软件观察或请求，不能证明设备已经断电；欠压保护不会等待日志落盘。
`event` is a combinable mask: 1 periodic/first sample, 2 enable, 4 disable, 8 charger/CX change, 16 full-decision change, 32 validity change, 64 long awake gap, 128 locked-wait start, 256 wait end, 512 power-off request, 1024 restart request, 2048 undervoltage-protection request. Events describe software observations or requests, not proof of power loss; undervoltage protection never waits for logging.

日志最多保留约 4 MiB 的 `batt.log` 和上一份 `batt.log.1`；单条记录可能略超轮转门槛。已有旧日志先继续保留并追加新会话头，达到门槛后正常轮转。每条检查写入、刷新、同步和关闭结果；失败后下一条重新写元信息。队列为 16 条，满时丢最旧记录并递增 `dropped`。无卡或 USB 交出 TF 时保留串口输出并累计 `file_missed`，不无限重试；卸载或格式化前等待在途日志 I/O 结束，超时中止该存储操作。断电仍可能丢失记录或损坏文件，不能承诺只丢最后一行。
Logging retains approximately 4 MiB in `batt.log` and one previous `batt.log.1`, with a possible one-row overshoot. Existing legacy rows remain until normal rotation, with new metadata appended. Every record checks write, flush, sync and close results; failures require metadata again. The 16-record queue drops its oldest record on overflow and increments `dropped`. Missing/exported cards retain serial output and increment `file_missed` without endless retries. Unmount/format joins in-flight log I/O and aborts on timeout. Power loss can still lose records or damage files; last-row-only loss is not guaranteed.

校准优先使用版本已知、充满与停止使用条件明确的自有设备记录。累计值只覆盖所采到的区间，不是硬件库仑计，也不能用锁屏前后两次瞬时电流推算整夜容量。日志不能代替电流表验证待机功耗；芯片 SOC 和软件判满结果也不能直接作为校准真值。
Prefer logs from an identified build and device with known full-charge and discharge endpoints. Totals cover sampled intervals only; they are not hardware coulomb counters, and endpoint current samples cannot establish overnight capacity. Logs do not replace standby-current instrumentation, and neither gauge SOC nor software full decisions are calibration ground truth.

## 兼容边界 / Compatibility boundary

`include/pmu_protocol.h` 保留原 CW32 线格式常量、命令码与共用数据结构；`device_power.h` 保留应用正在使用的快照和 API。删除旧硬件后端不代表这些兼容标识也能删除。

`include/pmu_protocol.h` retains the CW32 wire-format constants, command codes and
shared structures. `device_power.h` retains application snapshot and API contracts.
Removing the old hardware backend does not remove these compatibility identifiers.

`docs/` 中的协议头、寄存器表、中英文说明和 PDF 是旧 CW32 协议参考资料，不是当前 Metalio 硬件规格。
The protocol headers, register tables, bilingual guides and PDFs under `docs/` are
historical CW32 protocol references, not current Metalio hardware specifications.

## 验证 / Validation

```sh
python3 tools/run_reader_baseline.py power_key loop metalio_drivers battery battery_filter battery_log battery_log_task storage
```

采样与滤波测试执行当前 Metalio 实现；存储组同时覆盖 S31 外设睡眠恢复和应用下电交接。宿主测试不能替代真机电压、充电和关机验证。
Polling and filter tests execute the current Metalio implementation. The storage
suite also covers S31 peripheral sleep restoration and application power-off
handoff. Host tests do not replace device voltage, charging or power-off validation.

## 许可 / License

原有部分为 Apache-2.0，见 [LICENSE](LICENSE)；移植原厂部分保留 [MIT 声明](../board_i2c/LICENSE.Metalio)。
Existing portions use Apache-2.0; adapted vendor material retains the MIT notice linked above.
