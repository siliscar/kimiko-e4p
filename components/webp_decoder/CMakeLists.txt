# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
# 中文：仅编译可移植解码子集，内存由外部适配接管。
# English: Build only the portable decoder subset with external allocation adapters.
file(GLOB webp_sources
    "libwebp/src/dec/*.c"
    "libwebp/src/dsp/*.c"
    "libwebp/src/utils/*.c")
idf_component_register(SRCS ${webp_sources}
    INCLUDE_DIRS "libwebp/src"
    PRIV_INCLUDE_DIRS "libwebp" "include"
    PRIV_REQUIRES heap pthread)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    HAVE_CONFIG_H WEBP_MAX_ALLOCABLE_MEMORY=8388608)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -include "${CMAKE_CURRENT_LIST_DIR}/webp_alloc.h")
