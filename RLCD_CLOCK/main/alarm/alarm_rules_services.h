// 声明重复闹钟引擎：分钟级调度、响铃与对外增删改查接口。
// 与既有单次闹钟引擎并行；响铃复用同一音频管线，任意按键停止。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "alarm_rule.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化并加载规则；须在创建任务前调用。返回 false 表示互斥量或 NVS 初始化失败。
bool alarm_rules_init();

// FreeRTOS 任务入口；栈与优先级由创建方决定（与 alarm 任务同级即可）。
void alarm_rules_task(void *arg);

struct AlarmRulesListResult {
    AlarmRule rules[ALARM_RULE_MAX_COUNT];
    int count; // 实际条数；-1 表示互斥量不可用
};

// 拷贝当前规则列表（内存缓存，不加 NVS）。
bool alarm_rules_list(AlarmRulesListResult *out);

// 新增规则：自动分配 id，返回规则 id；0 表示失败（满/参数错/存储失败）。
uint8_t alarm_rules_add(const AlarmRule *template_rule);

// 更新已存在规则的内容（保留 id）。返回 false 表示不存在或存储失败。
bool alarm_rules_update(const AlarmRule *rule);

// 删除规则。返回 false 表示存储失败（不存在视为成功）。
bool alarm_rules_remove(uint8_t id);

// 启用/禁用规则。返回 false 表示不存在或存储失败。
bool alarm_rules_set_enabled(uint8_t id, bool enabled);

// 查询下一次将触发的启用规则；无则返回 false。
bool alarm_rules_next_trigger(AlarmRule *rule_out, time_t *next_epoch_out);

// 供按键路径停止响铃；正在响返回 true。
bool alarm_rules_stop_ringing_from_button();

// 规则变更或系统时间变化后由外部唤醒调度。
void alarm_rules_notify_changed();

#ifdef __cplusplus
}
#endif

// 注册 self.alarm.rules MCP 工具业务 handler；在 alarm_rules_init 后调用。
bool alarm_rules_mcp_register();
