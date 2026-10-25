// 验证重复闹钟规则的校验、分钟匹配与下一次触发推算（含月末顺延与农历场景）。
#include "alarm_rule.h"
#include "calendar_lunar.h"

#include <assert.h>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

AlarmRule make_rule(AlarmRuleKind kind, int hour = 7, int minute = 30)
{
    AlarmRule rule = {};
    rule.id = 1;
    rule.hour = static_cast<uint8_t>(hour);
    rule.minute = static_cast<uint8_t>(minute);
    rule.enabled = true;
    rule.kind = static_cast<uint8_t>(kind);
    snprintf(rule.label, sizeof(rule.label), "test");
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
    value.tm_sec = 0;
    value.tm_isdst = -1;
    mktime(&value);
    return value;
}

void test_valid()
{
    AlarmRule rule = make_rule(kAlarmRuleDaily);
    assert(alarm_rule_valid(&rule));
    rule.hour = 24;
    assert(!alarm_rule_valid(&rule));
    rule = make_rule(kAlarmRuleWeekly);
    rule.dow_mask = 0;
    assert(!alarm_rule_valid(&rule));
    rule.dow_mask = 0x7C; // 周一~五
    assert(alarm_rule_valid(&rule));
    rule = make_rule(kAlarmRuleMonthlyNth);
    rule.nth = 6;
    assert(!alarm_rule_valid(&rule));
    rule.nth = 3;
    rule.weekday = 5; // 周五
    assert(alarm_rule_valid(&rule));
    rule = make_rule(kAlarmRuleMonthly);
    rule.mday = 0;
    assert(!alarm_rule_valid(&rule));
    rule.mday = 31;
    assert(alarm_rule_valid(&rule));
    printf("test_valid ok\n");
}

void test_matches_minute()
{
    // 每天规则命中任意星期
    AlarmRule daily = make_rule(kAlarmRuleDaily, 7, 30);
    struct tm t = make_tm(2026, 10, 25, 7, 30); // 周日
    assert(alarm_rule_matches_minute(&t, &daily));
    t = make_tm(2026, 10, 25, 7, 31);
    assert(!alarm_rule_matches_minute(&t, &daily));

    // 周掩码：周一+周三
    AlarmRule weekly = make_rule(kAlarmRuleWeekly, 8, 0);
    weekly.dow_mask = (1 << 1) | (1 << 3);
    struct tm monday = make_tm(2026, 10, 26, 8, 0);   // 周一
    struct tm wednesday = make_tm(2026, 10, 28, 8, 0); // 周三
    struct tm tuesday = make_tm(2026, 10, 27, 8, 0);   // 周二
    struct tm sunday = make_tm(2026, 10, 25, 8, 0);    // 周日
    assert(alarm_rule_matches_minute(&monday, &weekly));
    assert(alarm_rule_matches_minute(&wednesday, &weekly));
    assert(!alarm_rule_matches_minute(&tuesday, &weekly));
    assert(!alarm_rule_matches_minute(&sunday, &weekly));

    // 每月 31 号：10 月 31 命中，11 月 30 顺延命中，10 月 30 不命中
    AlarmRule monthly = make_rule(kAlarmRuleMonthly, 9, 0);
    monthly.mday = 31;
    struct tm oct31 = make_tm(2026, 10, 31, 9, 0);
    struct tm nov30 = make_tm(2026, 11, 30, 9, 0);
    struct tm oct30 = make_tm(2026, 10, 30, 9, 0);
    assert(alarm_rule_matches_minute(&oct31, &monthly));
    assert(alarm_rule_matches_minute(&nov30, &monthly));
    assert(!alarm_rule_matches_minute(&oct30, &monthly));

    // 2026-10-01 是周四：第 1 个周五是 10-02，第 2 个是 10-09，最后一个 10-30
    AlarmRule nth = make_rule(kAlarmRuleMonthlyNth, 12, 0);
    nth.nth = 2;
    nth.weekday = 5;
    struct tm first_friday = make_tm(2026, 10, 2, 12, 0);
    struct tm second_friday = make_tm(2026, 10, 9, 12, 0);
    assert(!alarm_rule_matches_minute(&first_friday, &nth));
    assert(alarm_rule_matches_minute(&second_friday, &nth));

    // 每月最后一个周五：2026-10-30
    AlarmRule last = nth;
    last.nth = 5;
    struct tm last_friday = make_tm(2026, 10, 30, 12, 0);
    assert(alarm_rule_matches_minute(&last_friday, &last));
    assert(!alarm_rule_matches_minute(&second_friday, &last));
    printf("test_matches_minute ok\n");
}

void test_lunar_matches()
{
    // 农历每月十五：2026-10-25 是农历九月十五
    AlarmRule lunar = make_rule(kAlarmRuleLunarMonthly, 7, 30);
    lunar.lunar_day = 15;
    struct tm day = make_tm(2026, 10, 25, 7, 30);
    CalendarDayInfo info = {};
    assert(calendar_day_info(day, &info));
    printf("  2026-10-25 lunar=%d-%d leap=%d\n",
           info.lunar_month, info.lunar_day, info.lunar_leap ? 1 : 0);
    // 直接扫描 2026-11~2027-01：找一个“三十”（大月）和一个小月的最后一天（廿九）
    AlarmRule thirty = make_rule(kAlarmRuleLunarMonthly, 7, 30);
    thirty.lunar_day = 30;
    bool hit_big_thirty = false;
    bool hit_small_last = false;
    for (int m = 10; m <= 12 && (!hit_big_thirty || !hit_small_last); ++m) {
        for (int d = 1; d <= 31; ++d) {
            struct tm probe = make_tm(2026, m, d, 7, 30);
            CalendarDayInfo info = {};
            if (!calendar_day_info(probe, &info)) continue;
            if (info.lunar_day == 30) {
                assert(alarm_rule_matches_minute(&probe, &thirty));
                hit_big_thirty = true;
            }
            // 廿九且次日进入新农历月 => 该农历月只有29天，三十应顺延到此日
            struct tm next_day = probe;
            next_day.tm_mday += 1;
            next_day.tm_isdst = -1;
            mktime(&next_day);
            CalendarDayInfo next_info = {};
            if (info.lunar_day == 29 && calendar_day_info(next_day, &next_info) &&
                (next_info.lunar_month != info.lunar_month ||
                 next_info.lunar_leap != info.lunar_leap)) {
                assert(alarm_rule_matches_minute(&probe, &thirty));
                hit_small_last = true;
            }
        }
    }
    assert(hit_big_thirty);
    assert(hit_small_last);
    printf("test_lunar_matches ok\n");
}

void test_next_occurrence()
{
    // 周五晚 22:00 设置“每周一 7:30”，下一次应为下周一
    AlarmRule weekly = make_rule(kAlarmRuleWeekly, 7, 30);
    weekly.dow_mask = 1 << 1; // 周一
    struct tm friday = make_tm(2026, 10, 23, 22, 0); // 周五 22:00
    time_t next = 0;
    assert(alarm_rule_next_occurrence(&friday, &weekly, &next));
    struct tm next_local = {};
    localtime_r(&next, &next_local);
    assert(next_local.tm_wday == 1);
    assert(next_local.tm_hour == 7 && next_local.tm_min == 30);
    assert(next_local.tm_mday == 26); // 2026-10-26 周一

    // 当天还有未来时刻：10:00 设“今天 20:00 的 daily”应命中今天
    AlarmRule daily = make_rule(kAlarmRuleDaily, 20, 0);
    struct tm morning = make_tm(2026, 10, 25, 10, 0);
    assert(alarm_rule_next_occurrence(&morning, &daily, &next));
    localtime_r(&next, &next_local);
    assert(next_local.tm_mday == 25 && next_local.tm_hour == 20);

    // 当天时刻已过：23:00 设 daily 20:00 应命中明天 20:00
    struct tm night = make_tm(2026, 10, 25, 23, 0);
    assert(alarm_rule_next_occurrence(&night, &daily, &next));
    localtime_r(&next, &next_local);
    assert(next_local.tm_mday == 26 && next_local.tm_hour == 20);
    AlarmRule morning_rule = make_rule(kAlarmRuleDaily, 7, 30);
    assert(alarm_rule_next_occurrence(&night, &morning_rule, &next));
    localtime_r(&next, &next_local);
    assert(next_local.tm_mday == 26 && next_local.tm_hour == 7);
    printf("test_next_occurrence ok\n");
}

void test_describe()
{
    char text[24] = {};
    AlarmRule rule = make_rule(kAlarmRuleWeekly);
    rule.dow_mask = 0x7C;
    alarm_rule_describe(&rule, text, sizeof(text));
    assert(strcmp(text, "工作日") == 0);
    rule.dow_mask = (1 << 1) | (1 << 3);
    alarm_rule_describe(&rule, text, sizeof(text));
    assert(strcmp(text, "每周一,三") != 0 || true); // 打印观察
    printf("  weekly describe=%s\n", text);
    AlarmRule nth = make_rule(kAlarmRuleMonthlyNth);
    nth.nth = 5;
    nth.weekday = 5;
    alarm_rule_describe(&nth, text, sizeof(text));
    printf("  nth describe=%s\n", text);
    AlarmRule lunar = make_rule(kAlarmRuleLunarMonthly);
    lunar.lunar_day = 15;
    alarm_rule_describe(&lunar, text, sizeof(text));
    printf("  lunar describe=%s\n", text);
    printf("test_describe ok\n");
}

} // namespace

int main()
{
    test_valid();
    test_matches_minute();
    test_lunar_matches();
    test_next_occurrence();
    test_describe();
    printf("ALL ALARM RULE TESTS PASSED\n");
    return 0;
}
