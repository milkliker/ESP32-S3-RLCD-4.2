// 验证重复闹钟规则集 NVS 存储的装载、保存、擦除与错误语义（内存 stub）。
#include "alarm_rules_storage.h"

#include <assert.h>
#include <map>
#include <string>
#include <vector>

#include "nvs.h"

namespace {
constexpr nvs_handle_t kHandle = 9;
std::map<std::string, std::vector<uint8_t>> g_blobs;
esp_err_t g_open_error = ESP_OK;
esp_err_t g_commit_error = ESP_OK;
esp_err_t g_erase_key_error = ESP_OK;
int g_fail_nth_set = -1;
int g_set_calls = 0;

void reset_store()
{
    g_blobs.clear();
    g_open_error = ESP_OK;
    g_commit_error = ESP_OK;
    g_erase_key_error = ESP_OK;
    g_fail_nth_set = -1;
    g_set_calls = 0;
}

AlarmRule make_rule(uint8_t id, int hour, AlarmRuleKind kind)
{
    AlarmRule rule = {};
    rule.id = id;
    rule.hour = static_cast<uint8_t>(hour);
    rule.minute = 30;
    rule.enabled = true;
    rule.kind = static_cast<uint8_t>(kind);
    if (kind == kAlarmRuleWeekly) {
        rule.dow_mask = 0x7C;
    }
    if (kind == kAlarmRuleMonthly) {
        rule.mday = 15;
    }
    if (kind == kAlarmRuleLunarMonthly) {
        rule.lunar_day = 15;
    }
    snprintf(rule.label, sizeof(rule.label), "r%u", id);
    return rule;
}
} // namespace

esp_err_t nvs_open(const char *, nvs_open_mode_t, nvs_handle_t *out)
{
    if (g_open_error != ESP_OK) {
        return g_open_error;
    }
    *out = kHandle;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    assert(handle == kHandle);
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length)
{
    assert(handle == kHandle);
    auto found = g_blobs.find(key);
    if (found == g_blobs.end()) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    size_t copy = found->second.size() < *length ? found->second.size() : *length;
    memcpy(out, found->second.data(), copy);
    *length = found->second.size();
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t length)
{
    assert(handle == kHandle);
    ++g_set_calls;
    if (g_fail_nth_set == g_set_calls) {
        return ESP_FAIL;
    }
    auto &blob = g_blobs[key];
    blob.assign(static_cast<const uint8_t *>(data),
                static_cast<const uint8_t *>(data) + length);
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    assert(handle == kHandle);
    if (g_erase_key_error != ESP_OK) {
        return g_erase_key_error;
    }
    if (g_blobs.erase(key) == 0) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t handle)
{
    assert(handle == kHandle);
    g_blobs.clear();
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == kHandle);
    return g_commit_error;
}

int main()
{
    // 空库装载
    reset_store();
    AlarmRule rules[ALARM_RULE_MAX_COUNT] = {};
    assert(alarm_rules_storage::load(rules, ALARM_RULE_MAX_COUNT) == 0);

    // 保存 3 条不同类型并读回
    AlarmRule a = make_rule(1, 7, kAlarmRuleDaily);
    AlarmRule b = make_rule(2, 8, kAlarmRuleWeekly);
    AlarmRule c = make_rule(3, 9, kAlarmRuleLunarMonthly);
    assert(alarm_rules_storage::save(&a) == 0);
    assert(alarm_rules_storage::save(&b) == 0);
    assert(alarm_rules_storage::save(&c) == 0);
    assert(alarm_rules_storage::load(rules, ALARM_RULE_MAX_COUNT) == 3);
    assert(rules[0].id == 1 && rules[0].hour == 7 && rules[0].enabled);
    assert(rules[1].dow_mask == 0x7C);
    assert(rules[2].lunar_day == 15);
    assert(strcmp(rules[2].label, "r3") == 0);

    // 更新覆盖
    b.hour = 23;
    assert(alarm_rules_storage::save(&b) == 0);
    assert(alarm_rules_storage::load(rules, ALARM_RULE_MAX_COUNT) == 3);
    bool found23 = false;
    for (int i = 0; i < 3; ++i) {
        if (rules[i].id == 2) {
            found23 = rules[i].hour == 23;
        }
    }
    assert(found23);

    // 单条删除
    assert(alarm_rules_storage::erase(2) == 0);
    assert(alarm_rules_storage::load(rules, ALARM_RULE_MAX_COUNT) == 2);
    // 删除不存在的键视为成功
    assert(alarm_rules_storage::erase(2) == 0);

    // 全清
    assert(alarm_rules_storage::erase_all() == 0);
    assert(alarm_rules_storage::load(rules, ALARM_RULE_MAX_COUNT) == 0);

    // 无效规则拒绝写入
    AlarmRule bad = make_rule(4, 7, kAlarmRuleDaily);
    bad.hour = 99;
    assert(alarm_rules_storage::save(&bad) == -2);

    // 写失败与 commit 失败语义
    AlarmRule ok = make_rule(5, 6, kAlarmRuleDaily);
    g_set_calls = 0;
    g_fail_nth_set = 1;
    assert(alarm_rules_storage::save(&ok) == -2);
    g_fail_nth_set = -1;
    g_commit_error = ESP_FAIL;
    assert(alarm_rules_storage::save(&ok) == -2);
    g_commit_error = ESP_OK;

    // 打开失败语义
    g_open_error = ESP_ERR_NVS_NOT_INITIALIZED;
    assert(alarm_rules_storage::save(&ok) == -1);
    assert(alarm_rules_storage::erase(1) == -1);
    assert(alarm_rules_storage::erase_all() == -1);
    // 读路径打开失败视为空表（首次启动语义）
    assert(alarm_rules_storage::load(rules, ALARM_RULE_MAX_COUNT) == 0);

    printf("ALL ALARM RULES STORAGE TESTS PASSED\n");
    return 0;
}
