// 实现重复闹钟规则的参数校验、下一次触发推算与分钟匹配。
// 全部为纯逻辑，可在宿主环境单测；农历查询复用 ui/calendar_lunar 的本地算法。
#include "alarm_rule.h"

#include <stdio.h>
#include <string.h>

#include "calendar_lunar.h"

namespace {

constexpr int kDaysPerWeek = 7;
constexpr uint8_t kDowBitSunday = 0x01; // tm_wday 0=周日
constexpr uint8_t kWeekdaysMask = 0x7C; // 周一..周五
constexpr uint8_t kNthLast = 5;
constexpr int kLunarMaxDay = 30;

uint8_t dow_bit(int tm_wday)
{
    return static_cast<uint8_t>(1u << (tm_wday % kDaysPerWeek));
}

void normalize_tm(struct tm *value)
{
    value->tm_sec = 0;
    value->tm_isdst = -1;
}

// 返回当天所在农历月的总天数（次日进入新农历月则当天为月末）。
int lunar_month_length(const struct tm &local)
{
    struct tm next = local;
    next.tm_mday += 1;
    next.tm_sec = 0;
    next.tm_min = 0;
    next.tm_hour = 12;
    next.tm_isdst = -1;
    time_t next_epoch = mktime(&next);
    if (next_epoch == static_cast<time_t>(-1)) {
        return kLunarMaxDay;
    }
    struct tm next_local = {};
    if (!localtime_r(&next_epoch, &next_local)) {
        return kLunarMaxDay;
    }
    CalendarDayInfo today_info = {};
    CalendarDayInfo next_info = {};
    if (!calendar_day_info(local, &today_info) ||
        !calendar_day_info(next_local, &next_info)) {
        return kLunarMaxDay;
    }
    if (today_info.lunar_month != next_info.lunar_month ||
        today_info.lunar_leap != next_info.lunar_leap) {
        return today_info.lunar_day; // 今天是农历月末
    }
    return kLunarMaxDay;
}

bool is_last_day_of_gregorian_month(const struct tm *local)
{
    struct tm next = *local;
    next.tm_mday += 1;
    next.tm_hour = 12;
    next.tm_isdst = -1;
    mktime(&next);
    return next.tm_mday == 1;
}

bool monthly_nth_matches(const struct tm &local, const AlarmRule *rule)
{
    struct tm probe = local;
    probe.tm_mday = 1;
    normalize_tm(&probe);
    if (mktime(&probe) == static_cast<time_t>(-1)) {
        return false;
    }
    const int first_wday = probe.tm_wday;
    const int day_offset = local.tm_mday - 1;
    const int nth_index = (day_offset + first_wday) / kDaysPerWeek + 1;
    struct tm probe_end = local;
    probe_end.tm_mday += 1;
    probe_end.tm_isdst = -1;
    int last_mday = local.tm_mday;
    while (mktime(&probe_end) != static_cast<time_t>(-1) &&
           probe_end.tm_mon == local.tm_mon) {
        last_mday = probe_end.tm_mday;
        probe_end.tm_mday += 1;
        probe_end.tm_isdst = -1;
    }
    const int last_wday_index =
        ((last_mday - 1 + first_wday) / kDaysPerWeek) + 1;
    if (rule->nth >= kNthLast) {
        return nth_index == last_wday_index; // “最后一个”
    }
    return nth_index == rule->nth;
}

} // namespace

bool alarm_rule_valid(const AlarmRule *rule)
{
    if (!rule || rule->id == 0 || rule->hour > 23 || rule->minute > 59) {
        return false;
    }
    if (rule->kind > kAlarmRuleLunarMonthly) {
        return false;
    }
    switch (rule->kind) {
    case kAlarmRuleOnce:
    case kAlarmRuleDaily:
        return true;
    case kAlarmRuleWeekly:
        return rule->dow_mask != 0;
    case kAlarmRuleMonthly:
        return rule->mday >= 1 && rule->mday <= 31;
    case kAlarmRuleMonthlyNth:
        return rule->nth >= 1 && rule->nth <= kNthLast && rule->weekday < kDaysPerWeek;
    case kAlarmRuleLunarMonthly:
        return rule->lunar_day >= 1 && rule->lunar_day <= kLunarMaxDay;
    default:
        return false;
    }
}

bool alarm_quiet_hours_active(int hour, int minute)
{
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false; // 非法时间交给上层校验，不视为静音
    }
    constexpr int kQuietStartHour = 21;
    constexpr int kQuietStartMinute = 30;
    constexpr int kQuietEndHour = 6;
    constexpr int kQuietEndMinute = 30;
    if (hour > kQuietStartHour || (hour == kQuietStartHour && minute >= kQuietStartMinute)) {
        return true; // 21:30 - 23:59
    }
    if (hour < kQuietEndHour || (hour == kQuietEndHour && minute < kQuietEndMinute)) {
        return true; // 00:00 - 06:29
    }
    return false;
}

bool alarm_rule_is_recurring(const AlarmRule *rule)
{
    return rule && rule->kind != kAlarmRuleOnce;
}

bool alarm_rule_matches_minute(const struct tm *local, const AlarmRule *rule)
{
    if (!local || !rule || !rule->enabled || !alarm_rule_valid(rule)) {
        return false;
    }
    if (local->tm_hour != rule->hour || local->tm_min != rule->minute) {
        return false;
    }
    switch (rule->kind) {
    case kAlarmRuleOnce:
    case kAlarmRuleDaily:
        return true;
    case kAlarmRuleWeekly:
        return (dow_bit(local->tm_wday) & rule->dow_mask) != 0;
    case kAlarmRuleMonthly:
        if (local->tm_mday == rule->mday) {
            return true;
        }
        // 月内不存在该日号（如 2 月 30 日）则顺延到月末触发。
        return rule->mday > local->tm_mday && local->tm_mday >= 28 &&
               is_last_day_of_gregorian_month(local);
    case kAlarmRuleMonthlyNth:
        return local->tm_wday == rule->weekday &&
               monthly_nth_matches(*local, rule);
    case kAlarmRuleLunarMonthly: {
        CalendarDayInfo info = {};
        if (!calendar_day_info(*local, &info)) {
            return false;
        }
        if (info.lunar_day == rule->lunar_day) {
            return true;
        }
        // 农历月没有该日号（如小月 29 天设了 30 日）则顺延到农历月末。
        return info.lunar_day >= 29 && rule->lunar_day > info.lunar_day &&
               lunar_month_length(*local) == info.lunar_day;
    }
    default:
        return false;
    }
}

bool alarm_rule_next_occurrence(const struct tm *now_local,
                                const AlarmRule *rule,
                                time_t *out_epoch)
{
    if (!now_local || !rule || !out_epoch) {
        return false;
    }
    struct tm probe = *now_local;
    probe.tm_sec = 0;
    for (int day = 0; day < 366 * 2; ++day) {
        probe.tm_min = rule->minute;
        probe.tm_hour = rule->hour;
        probe.tm_isdst = -1;
        if (day == 0) {
            // 当天仅接受尚未过去的时刻。
            if (mktime(&probe) == static_cast<time_t>(-1)) {
                return false;
            }
            if (difftime(mktime(&probe), mktime(const_cast<struct tm *>(now_local))) <= 0) {
                probe = *now_local;
                probe.tm_mday += 1;
                continue;
            }
        } else {
            probe.tm_min = 0;
            probe.tm_hour = 0;
            probe.tm_isdst = -1;
            if (mktime(&probe) == static_cast<time_t>(-1)) {
                return false;
            }
            probe.tm_min = rule->minute;
            probe.tm_hour = rule->hour;
            probe.tm_isdst = -1;
            if (mktime(&probe) == static_cast<time_t>(-1)) {
                return false;
            }
        }
        if (alarm_rule_matches_minute(&probe, rule)) {
            *out_epoch = mktime(&probe);
            return *out_epoch != static_cast<time_t>(-1);
        }
        probe.tm_mday += 1;
    }
    return false;
}

void alarm_rule_describe(const AlarmRule *rule, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!rule) {
        return;
    }
    static const char *const kDowNames[] = {
        "日", "一", "二", "三", "四", "五", "六"};
    switch (rule->kind) {
    case kAlarmRuleOnce:
        snprintf(out, out_size, "单次");
        break;
    case kAlarmRuleDaily:
        snprintf(out, out_size, "每天");
        break;
    case kAlarmRuleWeekly: {
        if (rule->dow_mask == kWeekdaysMask) {
            snprintf(out, out_size, "工作日");
            break;
        }
        size_t used = 0;
        used += snprintf(out + used, out_size - used, "每周");
        for (int dow = 0; dow < kDaysPerWeek; ++dow) {
            if (rule->dow_mask & (1u << dow)) {
                used += snprintf(out + used, out_size - used, "%s", kDowNames[dow]);
                if (used >= out_size) {
                    break;
                }
            }
        }
        break;
    }
    case kAlarmRuleMonthly:
        snprintf(out, out_size, "每月%u号", rule->mday);
        break;
    case kAlarmRuleMonthlyNth:
        if (rule->nth >= kNthLast) {
            snprintf(out, out_size, "每月最后个周%s", kDowNames[rule->weekday % kDaysPerWeek]);
        } else {
            snprintf(out, out_size, "每月第%u个周%s", rule->nth, kDowNames[rule->weekday % kDaysPerWeek]);
        }
        break;
    case kAlarmRuleLunarMonthly:
        snprintf(out, out_size, "农历每月%u日", rule->lunar_day);
        break;
    default:
        snprintf(out, out_size, "未知");
        break;
    }
}
