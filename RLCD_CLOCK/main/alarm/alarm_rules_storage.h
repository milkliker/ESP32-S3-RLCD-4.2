// 声明重复闹钟规则集在独立 NVS 命名空间中的持久化接口。
// 每条规则一个 blob 键（r1..r32），删除即擦除键，掉电安全由 NVS 事务保证。
#pragma once

#include <stdint.h>

#include "alarm_rule.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

namespace alarm_rules_storage {

struct LoadResult {
    esp_err_t error = ESP_OK;
};

// 把全部规则读入 out（容量 max_count），返回实际条数；负数表示 NVS 打开失败。
int load(AlarmRule *out, int max_count);

// 写入或更新单条规则。返回 0 成功，-1 打开失败，-2 写入失败。
int save(const AlarmRule *rule);

// 按规则 id 擦除。返回 0 成功（含不存在），-1 打开失败，-2 擦除失败。
int erase(uint8_t id);

// 清空全部规则。返回 0 成功，-1 打开失败，-2 擦除失败。
int erase_all();

} // namespace alarm_rules_storage

#ifdef __cplusplus
}
#endif
