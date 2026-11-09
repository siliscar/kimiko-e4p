#pragma once

#include <stdbool.h>

// 快捷栏长按蓝牙圆键时，先指定要进入的设置子页，再切到设置页；on_enter 消费一次。
// return_to_origin 让子页左上角返回回到来源页，而不是设置主列表。
// The quick bar's Bluetooth hold names the settings sub-page to enter before the
// page switch; on_enter consumes it once. return_to_origin makes the sub-page's
// top-left back return to the origin page instead of the settings main list.
void app_device_settings_request_bluetooth(bool return_to_origin);
/// 自动更新确认入口；已在设置页时只打断主列表或升级页。/ Automatic offer entry; within Settings, interrupt only the root or updater.
bool app_device_settings_offer_update(bool already_in_settings);
