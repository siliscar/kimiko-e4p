/*
 * 中文：阅读会话的四方向重力稳定判定；不读硬件、不持久化、不改显示。
 * English: Four-way gravity stability for a reading session; no hardware reads, persistence or display mutations.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "../display/display_orientation.h"

typedef struct {
    bool automatic; ///< 当前会话自动旋转开关 / Automatic rotation for this session
    display_orientation_t locked; ///< 最近接受或手动锁定的方向 / Last accepted or manually locked orientation
    int candidate; ///< 候选方向加一，零表示未开始 / Candidate plus one; zero means no candidate
    int x, y, z; ///< 上一有效样本，用于拒绝晃动 / Last valid sample used to reject movement
    int64_t since_ms, last_ms; ///< 连续稳定起点与末次采样 / Continuous stable start and last sample
} reader_orientation_t;

/// 取消尚未完成的判断，保留会话开关与方向。/ Cancel an unfinished decision, retaining session mode and orientation.
void reader_orientation_cancel(reader_orientation_t *state);
/// 输入已转换的设备轴；平放、触摸、噪声、异常加速度或采样间断均不能提交方向。
/// Feed device axes; flat placement, touch, noise, abnormal acceleration and sampling gaps cannot commit orientation.
/// 返回 true 时给出稳定的新方向，由调用方事务式重排后接受。/ True proposes a stable new orientation for the caller to accept after transactional reflow.
bool reader_orientation_feed(reader_orientation_t *state, int x, int y, int z,
                             bool touched, int64_t now_ms, display_orientation_t *next);
