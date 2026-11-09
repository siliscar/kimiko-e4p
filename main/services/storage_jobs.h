/*
 * 中文：封面与头像文件任务的同步交还入口；由单一 UI 调度任务调用。
 * English: Synchronous handoff for cover and avatar file jobs, called by the UI scheduling task.
 */
#pragma once
#include <stdbool.h>

/// 先等待封面，再等待头像；任一步失败立即返回 false，timeout_ms 分别传给每个任务。
/// Wait for covers then avatars; return false at the first failure and pass timeout_ms to each job.
/// 只交还这两类任务；调用方仍负责阅读预绘、传输、OTA 及介质状态的门控。
/// Quiesce only these two job types; callers still gate reader prefetch, transfers, OTA and media state.
bool storage_jobs_quiesce(unsigned timeout_ms);
