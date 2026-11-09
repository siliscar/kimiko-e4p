# SPDX-FileCopyrightText: 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
# SPDX-FileCopyrightText: 2025 Project Contributors
# SPDX-License-Identifier: MIT
# 中文：移植 Metalio 2c1e5ef 的 PMU 时钟交接，仅编译构建目录副本，不改安装的 SDK。
# English: Adapt Metalio 2c1e5ef's PMU clock handoff using a build-local copy without changing the installed SDK.
# 中文：原厂许可见 components/board_i2c/LICENSE.Metalio；升级 SDK 时重新核对补丁和校准包装器。
# English: See components/board_i2c/LICENSE.Metalio; recheck the backport and calibration wrapper when upgrading the SDK.
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=pmu_sleep_start"
    "-Wl,--wrap=rtc_clk_mpll_configure")

set(_s31_pmu_path "$ENV{IDF_PATH}/components/esp_hw_support/port/esp32s31/pmu_sleep.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_s31_pmu_path}")
file(READ "${_s31_pmu_path}" _s31_pmu)
if(NOT _s31_pmu MATCHES "_psram_ctrlr_ll_select_clk_source")
    set(_s31_entry "s_mpll_freq_mhz_before_sleep = rtc_clk_mpll_get_freq();")
    set(_s31_exit "rtc_clk_mpll_configure(clk_hal_xtal_get_freq_mhz(), s_mpll_freq_mhz_before_sleep, false);")
    # 中文：锚点缺失或重复均拒绝构建，避免 SDK 改版后静默生成半套交接。
    # English: Reject missing or repeated anchors instead of silently generating a partial handoff after SDK changes.
    foreach(_s31_anchor IN ITEMS _s31_entry _s31_exit)
        string(REPLACE "${${_s31_anchor}}" "" _s31_without "${_s31_pmu}")
        string(LENGTH "${_s31_pmu}" _s31_before_length)
        string(LENGTH "${_s31_without}" _s31_after_length)
        string(LENGTH "${${_s31_anchor}}" _s31_anchor_length)
        math(EXPR _s31_removed "${_s31_before_length} - ${_s31_after_length}")
        if(NOT _s31_removed EQUAL _s31_anchor_length)
            message(FATAL_ERROR "S31 PMU source changed: review the sleep clock handoff backport")
        endif()
    endforeach()
    set(_s31_entry_body [=[
        if (s_mpll_freq_mhz_before_sleep) {
#if !BOOTLOADER_BUILD && CONFIG_SPIRAM
            _psram_ctrlr_ll_select_clk_source(PSRAM_CTRLR_LL_MSPI_ID_2, PSRAM_CLK_SRC_XTAL);
            _psram_ctrlr_ll_select_clk_source(PSRAM_CTRLR_LL_MSPI_ID_3, PSRAM_CLK_SRC_XTAL);
            if (!s_pmu_sleep_regdma_backup_enabled) {
                _psram_ctrlr_ll_enable_core_clock(PSRAM_CTRLR_LL_MSPI_ID_2, false);
                _psram_ctrlr_ll_enable_module_clock(PSRAM_CTRLR_LL_MSPI_ID_2, false);
            }
#endif
            rtc_clk_mpll_disable();
        }
]=])
    set(_s31_exit_body [=[
#if !BOOTLOADER_BUILD && CONFIG_SPIRAM
            if (!s_pmu_sleep_regdma_backup_enabled) {
                _psram_ctrlr_ll_enable_core_clock(PSRAM_CTRLR_LL_MSPI_ID_2, true);
                _psram_ctrlr_ll_enable_module_clock(PSRAM_CTRLR_LL_MSPI_ID_2, true);
            }
            _psram_ctrlr_ll_select_clk_source(PSRAM_CTRLR_LL_MSPI_ID_2, PSRAM_CLK_SRC_MPLL);
            _psram_ctrlr_ll_select_clk_source(PSRAM_CTRLR_LL_MSPI_ID_3, PSRAM_CLK_SRC_MPLL);
#endif
]=])
    string(REPLACE "${_s31_entry}" "${_s31_entry}\n${_s31_entry_body}" _s31_pmu "${_s31_pmu}")
    string(REPLACE "${_s31_exit}" "${_s31_exit}\n${_s31_exit_body}" _s31_pmu "${_s31_pmu}")
    file(WRITE "${CMAKE_BINARY_DIR}/s31_pmu_sleep_compat.c" "${_s31_pmu}")
    get_target_property(_s31_sources __idf_esp_hw_support SOURCES)
    list(FILTER _s31_sources EXCLUDE REGEX "port/esp32s31/pmu_sleep[.]c$")
    set_property(TARGET __idf_esp_hw_support PROPERTY SOURCES "${_s31_sources}")
    target_sources(__idf_esp_hw_support PRIVATE "${CMAKE_BINARY_DIR}/s31_pmu_sleep_compat.c")
endif()

# 中文：本地串口验证才输出每次睡眠结果，产品构建不增加周期日志。
# English: Emit per-sleep results only for local serial verification, without periodic product logs.
if("$ENV{KIIKO_SLEEP_DIAG}" STREQUAL "1")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_SLEEP_DIAG=1)
endif()
