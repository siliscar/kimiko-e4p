#!/usr/bin/env python3
"""中文：用真实驱动类型验证 S31 输入适配，不启动硬件或后台任务。
English: Validate S31 input adaptation against real driver types without hardware or background work.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="s31-input-") as folder:
    work = Path(folder)
    (work / "driver").mkdir()
    (work / "driver/gpio.h").write_text("#pragma once\ntypedef int gpio_num_t;\n")
    (work / "driver/i2c_master.h").write_text("#pragma once\ntypedef void *i2c_master_bus_handle_t;\n")
    (work / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_INVALID_STATE 2\n")
    (work / "sdkconfig.h").write_text("#pragma once\n#define CONFIG_IDF_TARGET_ESP32S31 1\n")
    (work / "test.c").write_text(r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "platform/s31_input.h"
#include "cst836u.h"
struct cst836u_dev_t { int unused; };
static struct cst836u_dev_t device;
static cst836u_touch_t raw, observed;
static esp_err_t result;
static bool write_payload, partial_payload;
static unsigned reads;
esp_err_t cst836u_read(cst836u_handle_t handle, cst836u_touch_t *touch) {
    ++reads;
    if (!handle || !touch) return ESP_ERR_INVALID_ARG;
    assert(handle == &device);
    observed = *touch;
    if (write_payload) *touch = raw;
    if (partial_payload) { touch->raw[0] ^= 255; touch->points[1].event = 7; }
    return result;
}
static void equal(const app_touch_t *touch, const cst836u_touch_t *expected) {
    assert(touch->touched == expected->touched && touch->count == expected->count);
    assert(touch->x == expected->x && touch->y == expected->y);
    for (unsigned i = 0; i < APP_TOUCH_MAX_POINTS; ++i) {
        assert(touch->points[i].active == expected->points[i].active);
        assert(touch->points[i].id == expected->points[i].id);
        assert(touch->points[i].event == expected->points[i].event);
        assert(touch->points[i].x == expected->points[i].x);
        assert(touch->points[i].y == expected->points[i].y);
    }
    assert(!memcmp(touch->raw, expected->raw, sizeof(touch->raw)));
}
int main(void) {
    app_touch_t touch = {0};
    assert(s31_input_read_touch(&device, NULL) == ESP_ERR_INVALID_ARG && reads == 0);
    write_payload = true;
    for (unsigned n = 0; n < 10000; ++n) {
        raw = (cst836u_touch_t){.touched=n%3!=0, .count=n%3,
            .x=(uint16_t)(n*17), .y=(uint16_t)(n*29)};
        for (unsigned i = 0; i < CST836U_MAX_POINTS; ++i)
            raw.points[i] = (cst836u_point_t){.active=n%2!=i, .id=(uint8_t)(n+i),
                .event=(uint8_t)(n>>2), .x=(uint16_t)(n*31+i), .y=(uint16_t)(n*43+i)};
        for (unsigned i = 0; i < CST836U_RAW_LEN; ++i) raw.raw[i] = (uint8_t)(n+i*13);
        result = n%5 ? ESP_OK : ESP_ERR_INVALID_STATE;
        assert(s31_input_read_touch(&device, &touch) == result);
        equal(&touch, &raw);
    }
    // 驱动不写或只改部分载荷时，旧快照及其余字段保持原样。
    // Preserve prior snapshots and untouched fields when the driver leaves or partially changes payloads.
    write_payload = false; result = ESP_ERR_INVALID_STATE;
    assert(s31_input_read_touch(&device, &touch) == result);
    equal(&touch, &raw); equal(&touch, &observed);
    assert(s31_input_read_touch(NULL, &touch) == ESP_ERR_INVALID_ARG);
    equal(&touch, &raw);
    partial_payload = true;
    raw.raw[0] ^= 255; raw.points[1].event = 7;
    assert(s31_input_read_touch(&device, &touch) == result); equal(&touch, &raw);
    partial_payload = false;
    write_payload = true; result = ESP_OK; raw = (cst836u_touch_t){0};
    assert(s31_input_read_touch(&device, &touch) == ESP_OK); equal(&touch, &raw);
    puts("PASS: S31 input real driver types; 10000 complete/multitouch/raw/error snapshots, unchanged early errors, null arguments and empty frames");
}
''')
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-I" + str(work), "-Imain", "-Imain/display",
        "-Icomponents/cst836u/include", str(work / "test.c"),
        "main/platform/s31_input.c", "-o", str(work / "test"),
    ], cwd=ROOT, check=True)
    subprocess.run([str(work / "test")], check=True)
    (work / "cst836u.h").write_text('#error "page contract must not import touch driver"\n')
    (work / "sc7a20h_lab.h").write_text('#error "page contract must not import accelerometer driver"\n')
    (work / "epdiy.h").write_text('#error "page contract must not import screen driver"\n')
    (work / "epd_highlevel.h").write_text('#error "page contract must not import backend state"\n')
    (work / "contract.c").write_text('#include "app/app.h"\napp_ctx_t page;\n')
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                    "-I" + str(work), "-Imain", "-Imain/display", str(work / "contract.c")], cwd=ROOT, check=True)
