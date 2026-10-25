// 实现重复闹钟规则集的 NVS 读写；键存在即有效，解析失败按缺失跳过。
#include "alarm_rules_storage.h"

#include <stdio.h>
#include <string.h>

#include "nvs.h"

namespace {
constexpr const char *kNamespace = "arules";
constexpr int kFirstId = 1;
constexpr int kLastId = ALARM_RULE_MAX_COUNT;

void make_key(int id, char *out, size_t size)
{
    snprintf(out, size, "r%d", id);
}

bool decode_rule(const uint8_t *data, size_t length, AlarmRule *rule)
{
    if (length != sizeof(AlarmRule)) {
        return false;
    }
    memcpy(rule, data, sizeof(AlarmRule));
    rule->label[ALARM_RULE_LABEL_MAX - 1] = '\0';
    return alarm_rule_valid(rule);
}
} // namespace

namespace alarm_rules_storage {

int load(AlarmRule *out, int max_count)
{
    if (!out || max_count <= 0) {
        return 0;
    }
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return 0; // 首次启动尚无命名空间视为空表
    }
    int count = 0;
    for (int id = kFirstId; id <= kLastId && count < max_count; ++id) {
        char key[8];
        make_key(id, key, sizeof(key));
        AlarmRule rule = {};
        size_t length = sizeof(rule);
        if (nvs_get_blob(handle, key, &rule, &length) != ESP_OK) {
            continue;
        }
        if (!decode_rule(reinterpret_cast<const uint8_t *>(&rule), length, &rule)) {
            continue;
        }
        out[count++] = rule;
    }
    nvs_close(handle);
    return count;
}

int save(const AlarmRule *rule)
{
    if (!alarm_rule_valid(rule)) {
        return -2;
    }
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return -1;
    }
    char key[8];
    make_key(rule->id, key, sizeof(key));
    esp_err_t error = nvs_set_blob(handle, key, rule, sizeof(*rule));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error == ESP_OK ? 0 : -2;
}

int erase(uint8_t id)
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return -1;
    }
    char key[8];
    make_key(id, key, sizeof(key));
    esp_err_t error = nvs_erase_key(handle, key);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return 0;
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error == ESP_OK ? 0 : -2;
}

int erase_all()
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return -1;
    }
    esp_err_t error = nvs_erase_all(handle);
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error == ESP_OK ? 0 : -2;
}

} // namespace alarm_rules_storage
