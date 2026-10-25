// 声明重复闹钟规则的纯数据模型与下一次触发时间的纯计算逻辑。
// 规则不依赖 FreeRTOS/NVS，可在宿主环境单测。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// 触发 once 规则后由调用方删除记录；其余类型保持启用。
typedef enum {
    kAlarmRuleOnce = 0,        // 单次（兼容原单闹钟语义，响后删除）
    kAlarmRuleDaily = 1,       // 每天
    kAlarmRuleWeekly = 2,      // 每周：dow_mask 可多选
    kAlarmRuleMonthly = 3,     // 每月 mday 号
    kAlarmRuleMonthlyNth = 4,  // 每月第 nth 个 dow 星期（nth=5 表示最后一个）
    kAlarmRuleLunarMonthly = 5 // 农历每月 lunar_day 日（农历不存在则顺延到月末）
} AlarmRuleKind;

#define ALARM_RULE_LABEL_MAX 24
#define ALARM_RULE_MAX_COUNT 32

typedef struct {
    uint8_t id;      // 1..255，0 表示无效
    uint8_t hour;    // 0-23
    uint8_t minute;  // 0-59
    bool enabled;
    uint8_t kind;    // AlarmRuleKind
    uint8_t dow_mask;    // weekly: bit0=周日 .. bit6=周六
    uint8_t mday;        // monthly: 1-31
    uint8_t nth;         // monthly_nth: 1-5，5 同时表示“最后一个”
    uint8_t weekday;     // monthly_nth: 0=周日..6=周六
    uint8_t lunar_day;   // lunar_monthly: 农历日 1-30
    char label[ALARM_RULE_LABEL_MAX];
} AlarmRule;

// 规则参数校验（不含时间合法性以外的人为约束）。
bool alarm_rule_valid(const AlarmRule *rule);

// 规则本次触发后是否保留（重复类型）。
bool alarm_rule_is_recurring(const AlarmRule *rule);

// 计算规则在严格晚于 now_local 的下一次触发时刻（本地时间秒）。
// 返回 false 表示近期无触发（如 mday 不存在）。
bool alarm_rule_next_occurrence(const struct tm *now_local,
                                const AlarmRule *rule,
                                time_t *out_epoch);

// 规则是否命中给定本地时刻所在的分钟（用于任务循环逐分钟检查）。
bool alarm_rule_matches_minute(const struct tm *local, const AlarmRule *rule);

// 把规则渲染为简短中文描述（如 "工作日" “每周一,三” “每月15号” “每月第2个周五” “农历每月15"）。
// 写入 out 并保证 NUL 结尾；out_size 需 >= 24。
void alarm_rule_describe(const AlarmRule *rule, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
