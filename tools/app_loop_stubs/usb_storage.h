#pragma once
#include <stdbool.h>

#ifdef POWER_KEY_SAMPLER_USB_TEST
extern bool power_key_test_usb_active;
static inline bool usb_storage_active(void) { return power_key_test_usb_active; }
#else
static inline bool usb_storage_active(void) { return false; }
#endif
