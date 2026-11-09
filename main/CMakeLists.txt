# 产品只支持 S31；在组件依赖解析时拒绝其他目标，避免使用不匹配的板级配置。
# The product supports S31 only; reject other targets during dependency discovery before selecting incompatible board settings.
if(NOT IDF_TARGET STREQUAL "esp32s31")
    message(FATAL_ERROR "kimiko E-INK4 Plus requires IDF_TARGET=esp32s31")
endif()

set(main_requires
    board_support
    frontlight
    wifi_transfer
    book_search
    mobi_codec
    espressif__qrcode
    espressif__esp_jpeg
    # 渐进式 JPEG；基线仍走 esp_jpeg。/ Progressive JPEG; baseline stays on esp_jpeg.
    jpegdec
    # 渐进式 JPEG 全分辨率解码（SOF2 的 DC-only 1/8 会糊，stb 补全分辨率）。/ Full-res SOF2 via stb.
    stb_image
    espressif__libpng
    webp_decoder
    espressif__zlib
    espressif__esp_tinyusb
    # BLE HID 主机（蓝牙翻页器）。本仓库开了 MINIMAL_BUILD，组件不进依赖图的话连它的
    # Kconfig 都不会加载，sdkconfig.defaults 里的 CONFIG_BT_* 会被判为未知符号而丢弃。
    # BLE HID host (Bluetooth page-turner). This tree sets MINIMAL_BUILD, so a component
    # outside the dependency graph does not even load its Kconfig and the CONFIG_BT_*
    # entries in sdkconfig.defaults are dropped as unknown symbols.
    bt
    # 蓝牙翻页器本体。/ The page-turner itself.
    ble_page_turner
    e0470_epaper_waveform
    e0470_page_turn
    device_power
    fatfs
    wear_levelling
    bootloader_support
    spi_flash
    heap
    nvs_flash
    esp_hw_support
    esp_rom
    esp_driver_gpio
    esp_driver_usb_serial_jtag
    esp_driver_jpeg
    esp_app_format
    app_update
    esp_http_client
    mbedtls
    espressif__cjson
)
# 原始位图保留在源码中，只把可复现的压缩资源嵌入 app。/ Retain source bitmaps and embed only reproducible compressed resources in the app.
idf_build_get_property(image_python PYTHON)
set(builtin_image_dir "${CMAKE_CURRENT_BINARY_DIR}/builtin_images")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/assets/lock_4bpp.bin"
    "${CMAKE_CURRENT_LIST_DIR}/../tools/compress_builtin_images.py")
execute_process(COMMAND "${image_python}" "${CMAKE_CURRENT_LIST_DIR}/../tools/compress_builtin_images.py"
    --source "${CMAKE_CURRENT_LIST_DIR}/assets" --output "${builtin_image_dir}"
    RESULT_VARIABLE builtin_image_result)
if(NOT builtin_image_result EQUAL 0)
    message(FATAL_ERROR "Built-in image compression failed")
endif()
file(SIZE "${CMAKE_CURRENT_LIST_DIR}/assets/weather_backgrounds.bin" weather_background_size)
if(NOT weather_background_size EQUAL 310464)
    message(FATAL_ERROR "Weather masks must be regenerated with tools/generate_weather_backgrounds.py")
endif()
idf_component_register(
    SRCS
        "app_main.c"
        "factory/epd_test.c"
        "boot/boot_state.c"
        "apps/app_boot_recovery.c"
        "ota/ota_update.c"
        "ota/ota_online.c"
        "ota/ota_auto.c"
        "ota/ota_release.c"
        "display/display.c"
        "display/display_refresh.c"
        "display/display_refresh_policy.c"
        "display/display_refresh_context.c"
        "display/display_waveform_catalog.c"
        "diagnostics/reader_perf.c"
        "display/display_main_waveform.c"
        "display/display_reader_waveform.c"
        "display/display_normalize.c"
        "power/cache_wake_restore.c"
        "power/s31_sleep_compat.c"
        "services/storage_jobs.c"
        "services/weather_model.c"
        "services/weather_service.c"
        "services/reading_journal_model.c"
        "services/reading_journal.c"
        "power/sleep.c"
        "time/time_service.c"
        "lock/lock_pin.c"
        "lock/lock_pin_flow.c"
        "lock/lock_screen.c"
        "settings/settings.c"
        "settings/home_components.c"
        "services/haptics.c"
        "app/app_loop.c"
        "app/app_lifecycle.c"
        "app/app_input.c"
        "app/app_media.c"
        "app/app_navigation.c"
        "platform/s31_input.c"
        "app/app_quick_layer.c"
        "app/touch_sampler.c"
        "app/crash_note.c"
        "app/app_registry.c"
        "ui/ui_kit.c"
        "ui/ui_weather.c"
        "ui/ui_reading_journal.c"
        "ui/ui_reader_heading.c"
        "ui/ui_reader_settings.c"
        "ui/ui_builtin_image.c"
        "ui/ui_text_edit.c"
        "ui/ui_text_input.c"
        "ui/ui_ime.c"
        "ui/ui_keyboard.c"
        "ui/ui_gesture.c"
        "ui/ui_pinpad.c"
        "ui/ui_power_dialog.c"
        "ui/ui_quick_menu.c"
        "ui/ui_lock_feedback.c"
        "ui/ui_quick_menu_input.c"
        "ui/ui_click_feedback.c"
        "ui/ui_wifi_qr.c"
        "ui/datamatrix.c"
        "ui/ui_menu.c"
        "ui/ui_nav.c"
        "ui/ui_image_dither.c"
        "ui/ui_wallpaper.c"
        "ui/ui_avatar.c"
        "ui/ui_reader_background.c"
        "apps/app_book.c"
        "apps/book_toc.c"
        "apps/app_dashboard.c"
        "apps/app_components.c"
        "apps/app_reading_journal.c"
        "apps/app_files.c"
        "apps/file_tree.c"
        "apps/app_device_settings.c"
        "apps/app_image.c"
        "apps/app_transfer.c"
        "services/usb_storage.c"
        "book/book_store.c"
        "book/book_shelf_model.c"
        "book/book_shelf_category_state.c"
        "book/book_collection.c"
        "book/book_shelf_scan.c"
        "book/book_shelf_manage.c"
        "book/reader_session.c"
        "book/reader_chapter.c"
        "book/reader_open.c"
        "book/reader_prefetch.c"
        "book/reader_present.c"
        "book/reader_turn.c"
        "book/reader_reflow.c"
        "book/reader_chrome.c"
        "book/book_progress.c"
        "book/book_path_state.c"
        "book/book_category_ops.c"
        "apps/book_category_ui.c"
        "book/book_history_backup.c"
        "book/book_title.c"
        "book/book_ticket.c"
        "book/book_excerpt.c"
        "book/book_lock_collage.c"
        "book/book_lock_bookmark.c"
        "book/book_source.c"
        "book/book_index_cache.c"
        "book/book_txt.c"
        "book/book_epub.c"
        "book/book_mobi.c"
        "book/book_mobi_markup.c"
        "book/book_metadata.c"
        "book/book_cover.c"
        "image/image_decode.c"
        "image/image_webp.c"
        "image/book_jpeg_hw.c"
        "book/book_cover_jobs.c"
        "book/book_shelf_covers.c"
        "image/book_image_header.c"
        "book/book_cover_auto.c"
        "book/zip_reader.c"
        "book/html_text.c"
        "book/book_layout.c"
        "book/gbk.c"
        "font/ttf_font.c"
        "font/ttf_font_source.c"
        "font/ttf_block_cache.c"
        "font/ttf_glyph_cache.c"
        "services/app_font_context.c"
        "font/ui_font.c"
        "font/ui_hanzi.c"
        "factory/pmu_selftest.c"
        "factory/vcom_setup.c"
    REQUIRES ${main_requires}
    PRIV_INCLUDE_DIRS
        "." "app" "apps" "book" "image" "font" "ui" "services" "platform" "factory"
        "boot" "display" "lock" "power" "ota" "settings" "time" "diagnostics"
    EMBED_FILES
        "${builtin_image_dir}/lock_4bpp.kbz"
        "assets/avatar_4bpp.bin"
        "assets/builtin.ttf"
        "assets/weather_backgrounds.bin"
        "assets/ui-hanzi.bin"
        "assets/display-test.png"
)

# 包住 IDF 的 panic 处理器：崩溃现场在重启前落进 RTC，没有串口时那是唯一的现场。
# 链接选项挂在 main 上，INTERFACE 会带到最终链接；写在 project() 之后的全局属性则太晚。
# Wrap the IDF panic handler so the crash scene reaches RTC before a restart; without a console
# that is the only place it survives. The option hangs off main, whose INTERFACE carries it into
# the final link; a global property set after project() is too late for the linker to see it.
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")

# S31 浅睡时钟交接与唤醒校准。/ S31 sleep clock handoff and wake calibration.
include("${CMAKE_CURRENT_LIST_DIR}/power/s31_sleep_compat.cmake")

# 本地 A/B：强制软件封面解码，用来对比同一构建的硬件路径。
# Local A/B: force software cover decode to compare against the hardware path in the same build.
if("$ENV{KIIKO_DISABLE_HW_JPEG}" STREQUAL "1")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BOOK_JPEG_HW_FORCE_SOFTWARE=1)
endif()
