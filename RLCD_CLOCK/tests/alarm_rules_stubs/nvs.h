// 重复闹钟宿主测试 NVS 桩：仅声明被测代码用到的接口，实现由测试文件提供。
#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
using nvs_handle_t = uint32_t;
enum nvs_open_mode_t { NVS_READONLY, NVS_READWRITE };
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t length);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
esp_err_t nvs_erase_all(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);
