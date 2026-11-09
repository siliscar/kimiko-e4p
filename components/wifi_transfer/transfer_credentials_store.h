/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 多网络共用一个版本化 blob，兼容旧单网络；失败不更新公开快照。
 * One versioned blob holds multiple networks with legacy compatibility; failures leave snapshots unchanged.
 */
#pragma once
#ifndef WIFI_TRANSFER_NVS_TEST
#include "nvs.h"
#endif
#include "transfer_policy.h"

typedef struct {
    uint8_t version;
    uint8_t count;
    transfer_credentials_t items[WIFI_TRANSFER_SAVED_MAX];
} transfer_profiles_t;

static bool profiles_valid(const transfer_profiles_t *profiles) {
    if (profiles->version != 2 || profiles->count > WIFI_TRANSFER_SAVED_MAX) return false;
    for (size_t i = 0; i < profiles->count; ++i) {
        if (!credentials_valid(&profiles->items[i])) return false;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(profiles->items[i].ssid, profiles->items[j].ssid)) return false;
    }
    return true;
}

static esp_err_t load_profiles(transfer_profiles_t *out) {
    memset(out, 0, sizeof(*out));
    out->version = 2;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("rp_wifi", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    size_t size = sizeof(*out);
    err = nvs_get_blob(handle, "profiles", out, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // 空的新格式也是权威记录；仅缺键时读取旧格式，避免遗忘后复活旧网络。
        // An empty new record is authoritative; only a missing key may fall back to legacy data.
        transfer_credentials_t legacy = {0};
        size = sizeof(legacy);
        err = nvs_get_blob(handle, "credentials", &legacy, &size);
        if (err == ESP_OK && (size != sizeof(legacy) || !credentials_valid(&legacy)))
            err = ESP_ERR_INVALID_STATE;
        memset(out, 0, sizeof(*out));
        out->version = 2;
        if (err == ESP_OK) { out->items[0] = legacy; out->count = 1; }
        clear_secret(&legacy, sizeof(legacy));
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        size = sizeof(*out);
    }
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(*out) || !profiles_valid(out)) {
        clear_secret(out, sizeof(*out));
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }
    return ESP_OK;
}

static esp_err_t store_profiles(const transfer_profiles_t *next) {
    if (!profiles_valid(next)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("rp_wifi", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, "profiles", next, sizeof(*next));
    if (err == ESP_OK) {
        esp_err_t erased = nvs_erase_key(handle, "credentials");
        if (erased != ESP_OK && erased != ESP_ERR_NVS_NOT_FOUND) err = erased;
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t load_credentials(transfer_credentials_t *out) {
    memset(out, 0, sizeof(*out));
    transfer_profiles_t profiles;
    esp_err_t err = load_profiles(&profiles);
    if (err == ESP_OK && profiles.count) *out = profiles.items[0];
    clear_secret(&profiles, sizeof(profiles));
    return err;
}

static int profile_index(const transfer_profiles_t *profiles, const char *ssid) {
    for (size_t i = 0; i < profiles->count; ++i)
        if (!strcmp(profiles->items[i].ssid, ssid)) return (int)i;
    return -1;
}

static esp_err_t store_credentials(const transfer_credentials_t *next) {
    if (next && !credentials_valid(next)) return ESP_ERR_INVALID_ARG;
    transfer_profiles_t profiles = {.version = 2};
    esp_err_t err = next ? load_profiles(&profiles) : ESP_OK;
    if (err == ESP_OK && next) {
        int index = profile_index(&profiles, next->ssid);
        if (index < 0 && profiles.count == WIFI_TRANSFER_SAVED_MAX) err = ESP_ERR_NO_MEM;
        else {
            if (index < 0) index = profiles.count++;
            memmove(&profiles.items[1], &profiles.items[0], (size_t)index * sizeof(*next));
            profiles.items[0] = *next;
        }
    }
    if (err == ESP_OK) err = store_profiles(&profiles);
    clear_secret(&profiles, sizeof(profiles));
    return err;
}

static esp_err_t forget_credentials(const char *ssid, transfer_credentials_t *preferred) {
    memset(preferred, 0, sizeof(*preferred));
    transfer_profiles_t profiles;
    esp_err_t err = load_profiles(&profiles);
    if (err == ESP_OK) {
        int index = ssid ? profile_index(&profiles, ssid) : profiles.count ? 0 : -1;
        if (index < 0 && ssid) err = ESP_ERR_NOT_FOUND;
        else if (index >= 0) {
            --profiles.count;
            memmove(&profiles.items[index], &profiles.items[index + 1],
                    (profiles.count - (size_t)index) * sizeof(profiles.items[0]));
            clear_secret(&profiles.items[profiles.count], sizeof(profiles.items[0]));
        }
        if (err == ESP_OK) err = store_profiles(&profiles);
        if (err == ESP_OK && profiles.count) *preferred = profiles.items[0];
    }
    clear_secret(&profiles, sizeof(profiles));
    return err;
}
