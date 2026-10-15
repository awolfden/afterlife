// Defense in depth; linked-image review is still required. No storage initialization.
#include "sdkconfig.h"
#include "esp_rom_sys.h"
#include <cstdlib>
#if defined(CONFIG_SECURE_BOOT) || defined(CONFIG_SECURE_FLASH_ENC_ENABLED) || defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) || defined(CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK) || defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH)
#error "Storage-mutating/provisioning configuration forbidden for preservation build"
#endif
[[noreturn]] static void blocked(){esp_rom_printf("WATCHER_STORAGE_BLOCKED\n");abort();}
#define GUARD(name) extern "C" [[noreturn]] void __wrap_##name(...){blocked();}
GUARD(esp_flash_write)
GUARD(esp_flash_write_encrypted)
GUARD(esp_flash_erase_region)
GUARD(spi_flash_write)
GUARD(spi_flash_erase_range)
GUARD(esp_partition_write)
GUARD(esp_partition_write_raw)
GUARD(esp_partition_erase_range)
GUARD(nvs_flash_init)
GUARD(nvs_flash_init_partition)
GUARD(nvs_open)
GUARD(nvs_open_from_partition)
GUARD(nvs_commit)
// Wi-Fi's adapter table references setters even with NVS disabled. Guard the
// callbacks themselves as well as open/commit and low-level flash entry points.
GUARD(nvs_set_i8)
GUARD(nvs_set_u8)
GUARD(nvs_set_i16)
GUARD(nvs_set_u16)
GUARD(nvs_set_i32)
GUARD(nvs_set_u32)
GUARD(nvs_set_i64)
GUARD(nvs_set_u64)
GUARD(nvs_set_str)
GUARD(nvs_set_blob)
GUARD(nvs_erase_key)
GUARD(nvs_erase_all)
GUARD(nvs_flash_erase)
GUARD(nvs_flash_erase_partition)
