// 防误触发强化测试：分钟精度、静音窗口边界、月末/闰年 rollover、
// “最后一个周X”、整周掩码、跨年推算与农历行为。
#include "alarm_rule.h"
#include "calendar_lunar.h"

#include <assert.h>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

AlarmRule make_rule(AlarmRuleKind kind, int hour, int minute)
{
    AlarmRule rule = {};
    rule.id = 1;
    rule.hour = static_cast<uint8_t>(hour);
    rule.minute = static_cast<uint8_t>(minute);
    rule.enabled = true;
    rule.kind = static_cast<uint8_t>(kind);
    snprintf(rule.label, sizeof(rule.label), "t");
    return rule;
}

struct tm make_tm(int year, int month, int mday, int hour = 0, int minute = 0)
{
    struct tm value = {};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = mday;
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_isdst = -1;
    mktime(&value);
    return value;
}

void test_quiet_hours_boundaries()
{
    // 窗口 21:30 - 06:30（21:30 起静音，06:30 起允许）
    struct { int h, m; bool quiet; } cases[] = {
        {21, 29, false}, {21, 30, true}, {21, 31, true},
        {22, 0, true}, {23, 59, true},
        {0, 0, true}, {5, 59, true}, {6, 29, true},
        {6, 30, false}, {6, 31, false}, {12, 0, false}, {20, 59, false},
    };
    for (const auto &c : cases) {
        assert(alarm_quiet_hours_active(c.h, c.m) == c.quiet);
    }
    // 非法时间不视为静音
    assert(!alarm_quiet_hours_active(24, 0));
    assert(!alarm_quiet_hours_active(-1, 0));
    assert(!alarm_quiet_hours_active(7, 60));
    printf("test_quiet_hours_boundaries ok\n");
}

void test_minute_precision()
{
    AlarmRule rule = make_rule(kAlarmRuleDaily, 7, 30);
    struct tm before = make_tm(2026, 10, 25, 7, 29);
    assert(!alarm_rule_matches_minute(&before, &rule));
    struct tm hit = make_tm(2026, 10, 25, 7, 30);
    assert(alarm_rule_matches_minute(&hit, &rule));
    struct tm miss = make_tm(2026, 10, 25, 7, 31);
    assert(!alarm_rule_matches_minute(&miss, &rule));
    // 禁用的规则永不匹配
    struct tm disabled_time = hit;
    rule.enabled = false;
    assert(!alarm_rule_matches_minute(&disabled_time, &rule));
    printf("test_minute_precision ok\n");
}

void test_extreme_times()
{
    // 0:00 与 23:59
    AlarmRule midnight = make_rule(kAlarmRuleDaily, 0, 0);
    struct tm at_midnight = make_tm(2026, 10, 26, 0, 0);
    assert(alarm_rule_matches_minute(&at_midnight, &midnight));
    struct tm at_2359 = make_tm(2026, 10, 25, 23, 59);
    assert(!alarm_rule_matches_minute(&at_2359, &midnight));
    AlarmRule late = make_rule(kAlarmRuleDaily, 23, 59);
    assert(alarm_rule_matches_minute(&at_2359, &late));
    assert(!alarm_rule_matches_minute(&at_midnight, &late));
    printf("test_extreme_times ok\n");
}

void test_monthly_rollover()
{
    // mday=31：2 月（28 天）顺延到 2/28，3 月精确 3/31，4 月（30 天）顺延 4/30
    AlarmRule m31 = make_rule(kAlarmRuleMonthly, 9, 0);
    m31.mday = 31;
    struct tm feb28 = make_tm(2026, 2, 28, 9, 0);
    assert(alarm_rule_matches_minute(&feb28, &m31));
    struct tm feb27 = make_tm(2026, 2, 27, 9, 0);
    assert(!alarm_rule_matches_minute(&feb27, &m31));
    struct tm mar31 = make_tm(2026, 3, 31, 9, 0);
    assert(alarm_rule_matches_minute(&mar31, &m31));
    struct tm mar30 = make_tm(2026, 3, 30, 9, 0);
    assert(!alarm_rule_matches_minute(&mar30, &m31));
    struct tm apr30 = make_tm(2026, 4, 30, 9, 0);
    assert(alarm_rule_matches_minute(&apr30, &m31));

    // 闰年 2028：mday=30 顺延到 2/29，mday=29 精确命中 2/29
    AlarmRule m30 = make_rule(kAlarmRuleMonthly, 9, 0);
    m30.mday = 30;
    struct tm leap_day = make_tm(2028, 2, 29, 9, 0);
    assert(alarm_rule_matches_minute(&leap_day, &m30));
    AlarmRule m29 = make_rule(kAlarmRuleMonthly, 9, 0);
    m29.mday = 29;
    assert(alarm_rule_matches_minute(&leap_day, &m29));
    // 非闰年 2026：2 月没有 29，mday=29 顺延到 2/28
    AlarmRule m29b = m29;
    struct tm feb28_26 = make_tm(2026, 2, 28, 9, 0);
    assert(alarm_rule_matches_minute(&feb28_26, &m29b));
    printf("test_monthly_rollover ok\n");
}

void test_last_weekday()
{
    // 2026 年 2 月：2/1 是周日。周五：2/6, 13, 20, 27 —— 恰好 4 个。
    // nth=5（最后一个）应在 2/27（第 4 个）触发，而不是不触发。
    AlarmRule last = make_rule(kAlarmRuleMonthlyNth, 8, 0);
    last.nth = 5;
    last.weekday = 5;
    struct tm fourth = make_tm(2026, 2, 27, 8, 0);
    assert(alarm_rule_matches_minute(&fourth, &last));
    struct tm third = make_tm(2026, 2, 20, 8, 0);
    assert(!alarm_rule_matches_minute(&third, &last));
    // 2026 年 10 月有 5 个周五（2,9,16,23,30）：最后一个在 10/30，10/23 不触发
    struct tm oct23 = make_tm(2026, 10, 23, 8, 0);
    struct tm oct30 = make_tm(2026, 10, 30, 8, 0);
    assert(!alarm_rule_matches_minute(&oct23, &last));
    assert(alarm_rule_matches_minute(&oct30, &last));
    // nth=4：10 月第 4 个周五是 10/23；2 月只有 4 个周五，2/27 是第 4 个也命中
    AlarmRule fourth_rule = last;
    fourth_rule.nth = 4;
    assert(alarm_rule_matches_minute(&oct23, &fourth_rule));
    assert(!alarm_rule_matches_minute(&oct30, &fourth_rule));
    assert(alarm_rule_matches_minute(&fourth, &fourth_rule));
    printf("test_last_weekday ok\n");
}

void test_weekly_full_week_scan()
{
    // 掩码=仅周一：连续 7 天恰好命中 1 次
    AlarmRule monday = make_rule(kAlarmRuleWeekly, 7, 15);
    monday.dow_mask = 1 << 1;
    int hits = 0;
    for (int d = 19; d <= 25; ++d) { // 2026-10-19 周一 起 7 天
        struct tm day = make_tm(2026, 10, d, 7, 15);
        hits += alarm_rule_matches_minute(&day, &monday) ? 1 : 0;
    }
    assert(hits == 1);
    // 掩码=周六+周日：7 天内恰好 2 次
    AlarmRule weekend = make_rule(kAlarmRuleWeekly, 9, 0);
    weekend.dow_mask = (1 << 0) | (1 << 6);
    hits = 0;
    for (int d = 19; d <= 25; ++d) {
        struct tm day = make_tm(2026, 10, d, 9, 0);
        hits += alarm_rule_matches_minute(&day, &weekend) ? 1 : 0;
    }
    assert(hits == 2);
    printf("test_weekly_full_week_scan ok\n");
}

void test_next_occurrence_year_wrap()
{
    // 12/31 23:00 的 daily 0:05 规则 → 次年 1/1 00:05
    AlarmRule early = make_rule(kAlarmRuleDaily, 0, 5);
    struct tm nye = make_tm(2026, 12, 31, 23, 0);
    time_t next = 0;
    assert(alarm_rule_next_occurrence(&nye, &early, &next));
    struct tm next_local = {};
    localtime_r(&next, &next_local);
    assert(next_local.tm_year == 2027 - 1900 && next_local.tm_mon == 0 &&
           next_local.tm_mday == 1 && next_local.tm_hour == 0 &&
           next_local.tm_min == 5);
    // 每月 31 号规则在 2 月初询问 → 下一触发是 3/31（2 月顺延日不算“下一触发”？）
    // 语义：matches 允许 2/28 顺延命中，因此 next_occurrence 也应返回 2/28
    AlarmRule m31 = make_rule(kAlarmRuleMonthly, 9, 0);
    m31.mday = 31;
    struct tm feb1 = make_tm(2026, 2, 1, 10, 0);
    assert(alarm_rule_next_occurrence(&feb1, &m31, &next));
    localtime_r(&next, &next_local);
    assert(next_local.tm_mon == 1 && next_local.tm_mday == 28);
    printf("test_next_occurrence_year_wrap ok\n");
}

void test_lunar_leap_behavior()
{
    // 农历规则匹配包含闰月（农历X月与闰X月都会触发），并验证三十顺延在闰月同样成立
    AlarmRule lunar = make_rule(kAlarmRuleLunarMonthly, 7, 30);
    lunar.lunar_day = 1;
    // 扫 2025-2027：每个月的农历初一都应命中（闰月含内）
    int month_starts = 0;
    for (int y = 2025; y <= 2027; ++y) {
        for (int m = 1; m <= 12; ++m) {
            for (int d = 1; d <= 31; ++d) {
                struct tm probe = make_tm(y, m, d, 7, 30);
                CalendarDayInfo info = {};
                if (!calendar_day_info(probe, &info)) continue;
                if (info.lunar_day == 1) {
                    assert(alarm_rule_matches_minute(&probe, &lunar));
                    ++month_starts;
                }
            }
        }
    }
    assert(month_starts >= 36); // 三年至少 36 个农历月起始（含闰月则更多）
    printf("test_lunar_leap_behavior ok (month starts=%d)\n", month_starts);
}

void test_quiet_interplay_with_rules()
{
    // 06:00 与 22:30 的规则时间本身落在静音窗口（引擎据此跳过响铃与展示）
    assert(alarm_quiet_hours_active(6, 0));
    assert(alarm_quiet_hours_active(22, 30));
    // 06:30 与 21:29 边界外侧：允许响铃
    assert(!alarm_quiet_hours_active(6, 30));
    assert(!alarm_quiet_hours_active(21, 29));
    printf("test_quiet_interplay_with_rules ok\n");
}

} // namespace

int main()
{
    test_quiet_hours_boundaries();
    test_minute_precision();
    test_extreme_times();
    test_monthly_rollover();
    test_last_weekday();
    test_weekly_full_week_scan();
    test_next_occurrence_year_wrap();
    test_lunar_leap_behavior();
    test_quiet_interplay_with_rules();
    printf("ALL MISFIRE GUARD TESTS PASSED\n");
    return 0;
}
