// 重复闹钟宿主测试错误码桩。
#pragma once
using esp_err_t = int;
inline constexpr esp_err_t ESP_OK = 0;
inline constexpr esp_err_t ESP_FAIL = -1;
inline constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;
inline constexpr esp_err_t ESP_ERR_NVS_NOT_INITIALIZED = 0x1101;
