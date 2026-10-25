// 实现小智 MCP self.alarm.rules 工具的业务侧：请求转换为规则引擎调用与结果 JSON。
#include "alarm_rules_services.h"

#include "xiaozhi_mcp.h"

#include <cstdio>
#include <cstring>

namespace {
AlarmRule request_to_rule(const XiaozhiMcpAlarmRuleRequest &request)
{
    AlarmRule rule = {};
    rule.hour = static_cast<uint8_t>(request.hour);
    rule.minute = static_cast<uint8_t>(request.minute);
    rule.enabled = true;
    rule.kind = static_cast<uint8_t>(request.recur);
    rule.dow_mask = static_cast<uint8_t>(request.dow_mask);
    rule.mday = static_cast<uint8_t>(request.mday);
    rule.nth = static_cast<uint8_t>(request.nth);
    rule.weekday = static_cast<uint8_t>(request.weekday);
    rule.lunar_day = static_cast<uint8_t>(request.lunar_day);
    strlcpy(rule.label, request.label, sizeof(rule.label));
    return rule;
}

void describe_rule(const AlarmRule &rule, char *out, size_t size)
{
    char kind[24] = {};
    alarm_rule_describe(&rule, kind, sizeof(kind));
    const bool quiet_muted = alarm_quiet_hours_active(rule.hour, rule.minute);
    snprintf(out, size,
             "{\"id\":%u,\"time\":\"%02u:%02u\",\"repeat\":\"%s\",\"enabled\":%s"
             "%s%s%s,\"quiet_muted\":%s}",
             rule.id,
             rule.hour,
             rule.minute,
             kind,
             rule.enabled ? "true" : "false",
             rule.label[0] ? ",\"label\":\"" : "",
             rule.label[0] ? rule.label : "",
             rule.label[0] ? "\"" : "",
             quiet_muted ? "true" : "false");
}

bool handle_set(const XiaozhiMcpAlarmRuleRequest &request, char *result, size_t result_len)
{
    AlarmRule rule = request_to_rule(request);
    if (!alarm_rule_valid(&rule)) {
        snprintf(result, result_len, "alarm rule rejected: invalid parameters");
        return false;
    }
    uint8_t id = alarm_rules_add(&rule);
    if (id == 0) {
        snprintf(result, result_len,
                 "alarm rule rejected: storage full (max %d) or busy",
                 ALARM_RULE_MAX_COUNT);
        return false;
    }
    AlarmRule saved = rule;
    saved.id = id;
    char described[96] = {};
    describe_rule(saved, described, sizeof(described));
    const char *quiet_note =
        alarm_quiet_hours_active(rule.hour, rule.minute)
            ? ",\"note\":\"该时间处于21:30-06:30静音窗口，闹钟不会响铃\""
            : "";
    snprintf(result, result_len, "{\"created\":true,\"rule\":%s%s}", described, quiet_note);
    return true;
}

bool handle_list(char *result, size_t result_len)
{
    AlarmRulesListResult listing = {};
    if (!alarm_rules_list(&listing)) {
        snprintf(result, result_len, "alarm rules busy");
        return false;
    }
    size_t used = 0;
    used += static_cast<size_t>(
        snprintf(result + used, result_len - used, "{\"rules\":["));
    for (int i = 0; i < listing.count && used < result_len; ++i) {
        char described[96] = {};
        describe_rule(listing.rules[i], described, sizeof(described));
        used += static_cast<size_t>(snprintf(result + used,
                                             result_len - used,
                                             "%s%s",
                                             i > 0 ? "," : "",
                                             described));
    }
    (void)snprintf(result + used, result_len - used, "],\"count\":%d}", listing.count);
    return true;
}

bool handle_toggle(const XiaozhiMcpAlarmRuleRequest &request, char *result, size_t result_len)
{
    AlarmRulesListResult listing = {};
    if (!alarm_rules_list(&listing)) {
        snprintf(result, result_len, "alarm rules busy");
        return false;
    }
    bool exists = false;
    for (int i = 0; i < listing.count; ++i) {
        if (listing.rules[i].id == request.id) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        // 报告现有 id 列表，方便大模型先 list 再操作
        size_t used = 0;
        used += static_cast<size_t>(snprintf(result + used,
                                             result_len - used,
                                             "alarm rule id %d not found; existing ids:",
                                             request.id));
        for (int i = 0; i < listing.count && used < result_len; ++i) {
            used += static_cast<size_t>(snprintf(result + used,
                                                 result_len - used,
                                                 " %u",
                                                 listing.rules[i].id));
        }
        if (listing.count == 0) {
            (void)snprintf(result + used, result_len - used, " none");
        }
        return false;
    }
    if (!alarm_rules_set_enabled(static_cast<uint8_t>(request.id), request.enabled)) {
        snprintf(result, result_len, "alarm rule toggle failed");
        return false;
    }
    snprintf(result,
             result_len,
             "{\"id\":%d,\"enabled\":%s}",
             request.id,
             request.enabled ? "true" : "false");
    return true;
}

bool handle_remove(const XiaozhiMcpAlarmRuleRequest &request, char *result, size_t result_len)
{
    AlarmRulesListResult listing = {};
    if (!alarm_rules_list(&listing)) {
        snprintf(result, result_len, "alarm rules busy");
        return false;
    }
    bool exists = false;
    for (int i = 0; i < listing.count; ++i) {
        if (listing.rules[i].id == request.id) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        snprintf(result, result_len, "alarm rule id %d not found", request.id);
        return false;
    }
    if (!alarm_rules_remove(static_cast<uint8_t>(request.id))) {
        snprintf(result, result_len, "alarm rule remove failed");
        return false;
    }
    snprintf(result, result_len, "{\"removed\":true,\"id\":%d}", request.id);
    return true;
}

bool mcp_alarm_rules(const XiaozhiMcpAlarmRuleRequest &request,
                     char *result,
                     size_t result_len)
{
    switch (request.action) {
    case 0:
        return handle_set(request, result, result_len);
    case 1:
        return handle_list(result, result_len);
    case 2:
        return handle_toggle(request, result, result_len);
    case 3:
        return handle_remove(request, result, result_len);
    default:
        snprintf(result, result_len, "alarm rules rejected: unknown action");
        return false;
    }
}
} // namespace

bool alarm_rules_mcp_register()
{
    xiaozhi_mcp_register_alarm_rule_handler(mcp_alarm_rules);
    return true;
}
