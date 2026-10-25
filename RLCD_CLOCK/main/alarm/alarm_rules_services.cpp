// 实现重复闹钟引擎：加载/增删改查规则、分钟匹配调度和响铃。
// 响铃流程复用既有单次闹钟的模式：挂起小智音频 → 循环播放提示音 → 按键停止。
#include "alarm_rules_services.h"

#include "alarm_rules_storage.h"
#include "app_tick_time.h"
#include "scoped_semaphore_lock.h"
#include "audio_services.h"
#include "pomodoro_services.h"
#include "reminder_schedule.h"
#include "sensor_time.h"
#include "xiaozhi_ai.h"

#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_log.h>

namespace {
constexpr const char *kTag = "alarm_rules";
constexpr int kAlarmSoundIndex = 1;
constexpr uint32_t kRingMaximumMs = 60U * 1000U;
constexpr uint32_t kRingRepeatPauseMs = 5U * 1000U;
constexpr uint32_t kAudioReleaseWaitMs = 3000U;
constexpr uint32_t kAudioReleasePollMs = 20U;
constexpr uint32_t kTimeInvalidRetryMs = 5U * 1000U;
constexpr uint32_t kMinuteSlopMs = 2000U;

StaticTaskMutex s_mutex;
TaskHandle_t s_task_handle = nullptr;
std::atomic<bool> s_stop_ring{false};
std::atomic<bool> s_ringing{false};

AlarmRule s_rules[ALARM_RULE_MAX_COUNT];
int s_rule_count = 0;

// 已触发判定：记录最近一次响铃的规则 id 与所在分钟，防止同一分钟重复触发。
uint8_t s_last_fired_id = 0;
int64_t s_last_fired_minute_ms = -1;

bool rules_stop_callback()
{
    return s_stop_ring.load();
}

bool find_rule_unlocked(uint8_t id, int *index_out)
{
    for (int i = 0; i < s_rule_count; ++i) {
        if (s_rules[i].id == id) {
            if (index_out) {
                *index_out = i;
            }
            return true;
        }
    }
    return false;
}

uint8_t allocate_id_unlocked()
{
    for (int candidate = 1; candidate <= 255; ++candidate) {
        if (!find_rule_unlocked(static_cast<uint8_t>(candidate), nullptr)) {
            return static_cast<uint8_t>(candidate);
        }
    }
    return 0;
}

bool persist_save(const AlarmRule *rule)
{
    return alarm_rules_storage::save(rule) == 0;
}

bool persist_erase(uint8_t id)
{
    return alarm_rules_storage::erase(id) == 0;
}

void wake_task()
{
    if (s_task_handle) {
        xTaskNotifyGive(s_task_handle);
    }
}

bool pomodoro_conflicts_with_minute(const struct tm &local)
{
    if (!is_system_time_plausible()) {
        return false;
    }
    PomodoroSnapshot pomodoro = {};
    pomodoro_get_snapshot(&pomodoro);
    return pomodoro.state == kPomodoroRunning &&
           reminder_targets_same_local_minute(reminder_wall_clock_ms(),
                                              local.tm_hour,
                                              local.tm_min,
                                              pomodoro.remaining_ms);
}

void ring_blocking(const AlarmRule &rule)
{
    s_stop_ring.store(false);
    s_ringing.store(true);
    ESP_LOGI(kTag, "rule %u ring %02u:%02u %s",
             rule.id, rule.hour, rule.minute,
             rule.label[0] ? rule.label : "");
    xiaozhi_ai_set_alarm_suspended(true);
    (void)wait_for_audio_playback_idle(kAudioReleaseWaitMs,
                                       kAudioReleasePollMs,
                                       rules_stop_callback);
    TickType_t started = xTaskGetTickCount();
    const TickType_t maximum = pdMS_TO_TICKS(kRingMaximumMs);
    while (!s_stop_ring.load() &&
           xTaskGetTickCount() - started < maximum) {
        if (play_chime_sound_blocking(kAlarmSoundIndex, rules_stop_callback)) {
            TickType_t pause_started = xTaskGetTickCount();
            while (!s_stop_ring.load() &&
                   xTaskGetTickCount() - pause_started <
                       pdMS_TO_TICKS(kRingRepeatPauseMs)) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
    xiaozhi_ai_set_alarm_suspended(false);
    s_ringing.store(false);
    ESP_LOGI(kTag, "rule %u ring finished stopped=%d",
             rule.id, s_stop_ring.load() ? 1 : 0);
}

// 遍历启用的重复规则，命中当前分钟则响铃；once 规则响后删除。
void evaluate_minute(const struct tm &local, int64_t minute_ms)
{
    AlarmRule fired = {};
    bool has_fired = false;
    bool remove_after = false;
    {
        if (pomodoro_conflicts_with_minute(local)) {
            return;
        }
        ScopedSemaphoreLock lock(s_mutex.handle());
        if (!lock) {
            return;
        }
        for (int i = 0; i < s_rule_count; ++i) {
            AlarmRule &rule = s_rules[i];
            if (!rule.enabled || !alarm_rule_matches_minute(&local, &rule)) {
                continue;
            }
            if (rule.id == s_last_fired_id && minute_ms == s_last_fired_minute_ms) {
                continue;
            }
            s_last_fired_id = rule.id;
            s_last_fired_minute_ms = minute_ms;
            if (alarm_quiet_hours_active(rule.hour, rule.minute)) {
                // 夜间静音门禁：窗口内不出声也不消费，下一分钟自然不再匹配。
                continue;
            }
            fired = rule;
            has_fired = true;
            remove_after = !alarm_rule_is_recurring(&fired);
            if (remove_after) {
                s_rules[i] = s_rules[s_rule_count - 1];
                --s_rule_count;
            }
            break; // 一分钟内只触发一条
        }
    }
    if (!has_fired) {
        return;
    }
    ring_blocking(fired);
    if (remove_after && !persist_erase(fired.id)) {
        ESP_LOGW(kTag, "once rule %u erase failed", fired.id);
    }
}

uint32_t ms_to_next_minute_boundary(int64_t wall_clock_ms)
{
    const int64_t remainder = wall_clock_ms % 60000;
    int64_t remaining = 60000 - remainder;
    if (remaining < kMinuteSlopMs) {
        remaining += 60000; // 提前 2 秒唤醒，避免分钟边界抖动漏触发
    }
    return static_cast<uint32_t>(remaining);
}
} // namespace

bool alarm_rules_init()
{
    if (!s_mutex.init()) {
        return false;
    }
    ScopedSemaphoreLock lock(s_mutex.handle());
    if (!lock) {
        return false;
    }
    s_rule_count = alarm_rules_storage::load(s_rules, ALARM_RULE_MAX_COUNT);
    return s_rule_count >= 0;
}

void alarm_rules_task(void *)
{
    s_task_handle = xTaskGetCurrentTaskHandle();
    for (;;) {
        if (!is_system_time_plausible()) {
            vTaskDelay(pdMS_TO_TICKS(kTimeInvalidRetryMs));
            continue;
        }
        const int64_t wall_clock_ms = reminder_wall_clock_ms();
        const time_t seconds = static_cast<time_t>(wall_clock_ms / 1000);
        struct tm local = {};
        const bool time_valid = wall_clock_ms >= 0 &&
                                localtime_r(&seconds, &local) != nullptr &&
                                is_tm_plausible(local);
        if (!time_valid) {
            vTaskDelay(pdMS_TO_TICKS(kTimeInvalidRetryMs));
            continue;
        }
        evaluate_minute(local, wall_clock_ms - wall_clock_ms % 60000);
        const uint32_t wait_ms = ms_to_next_minute_boundary(wall_clock_ms);
        ulTaskNotifyTake(pdTRUE, app_tick_nonzero_delay(pdMS_TO_TICKS(wait_ms)));
    }
}

bool alarm_rules_list(AlarmRulesListResult *out)
{
    if (!out) {
        return false;
    }
    ScopedSemaphoreLock lock(s_mutex.handle());
    if (!lock) {
        out->count = -1;
        return false;
    }
    memcpy(out->rules, s_rules, sizeof(AlarmRule) * s_rule_count);
    out->count = s_rule_count;
    return true;
}

uint8_t alarm_rules_add(const AlarmRule *template_rule)
{
    if (!template_rule) {
        return 0;
    }
    AlarmRule rule = *template_rule;
    ScopedSemaphoreLock lock(s_mutex.handle());
    if (!lock) {
        return 0;
    }
    if (s_rule_count >= ALARM_RULE_MAX_COUNT) {
        return 0;
    }
    rule.id = allocate_id_unlocked();
    rule.enabled = true;
    if (!alarm_rule_valid(&rule) || !persist_save(&rule)) {
        return 0;
    }
    s_rules[s_rule_count++] = rule;
    wake_task();
    return rule.id;
}

bool alarm_rules_update(const AlarmRule *rule)
{
    if (!rule) {
        return false;
    }
    ScopedSemaphoreLock lock(s_mutex.handle());
    if (!lock) {
        return false;
    }
    int index = -1;
    if (!find_rule_unlocked(rule->id, &index) || !alarm_rule_valid(rule)) {
        return false;
    }
    if (!persist_save(rule)) {
        return false;
    }
    s_rules[index] = *rule;
    wake_task();
    return true;
}

bool alarm_rules_remove(uint8_t id)
{
    ScopedSemaphoreLock lock(s_mutex.handle());
    if (!lock) {
        return false;
    }
    int index = -1;
    if (!find_rule_unlocked(id, &index)) {
        return true;
    }
    if (!persist_erase(id)) {
        return false;
    }
    s_rules[index] = s_rules[s_rule_count - 1];
    --s_rule_count;
    wake_task();
    return true;
}

bool alarm_rules_set_enabled(uint8_t id, bool enabled)
{
    ScopedSemaphoreLock lock(s_mutex.handle());
    if (!lock) {
        return false;
    }
    int index = -1;
    if (!find_rule_unlocked(id, &index)) {
        return false;
    }
    AlarmRule updated = s_rules[index];
    updated.enabled = enabled;
    if (!persist_save(&updated)) {
        return false;
    }
    s_rules[index] = updated;
    wake_task();
    return true;
}

bool alarm_rules_next_trigger(AlarmRule *rule_out, time_t *next_epoch_out)
{
    if (!rule_out || !next_epoch_out) {
        return false;
    }
    const int64_t wall_clock_ms = reminder_wall_clock_ms();
    const time_t seconds = static_cast<time_t>(wall_clock_ms / 1000);
    struct tm local = {};
    if (wall_clock_ms < 0 || localtime_r(&seconds, &local) == nullptr) {
        return false;
    }
    bool found = false;
    time_t best = 0;
    AlarmRule best_rule = {};
    ScopedSemaphoreLock lock(s_mutex.handle());
    if (!lock) {
        return false;
    }
    for (int i = 0; i < s_rule_count; ++i) {
        if (!s_rules[i].enabled ||
            alarm_quiet_hours_active(s_rules[i].hour, s_rules[i].minute)) {
            // 静音窗口内的规则永远不会出声，不作为“下次触发”展示。
            continue;
        }
        time_t candidate = 0;
        if (alarm_rule_next_occurrence(&local, &s_rules[i], &candidate) &&
            (!found || candidate < best)) {
            found = true;
            best = candidate;
            best_rule = s_rules[i];
        }
    }
    if (found) {
        *rule_out = best_rule;
        *next_epoch_out = best;
    }
    return found;
}

bool alarm_rules_stop_ringing_from_button()
{
    if (!s_ringing.load()) {
        return false;
    }
    s_stop_ring.store(true);
    wake_task();
    return true;
}

void alarm_rules_notify_changed()
{
    wake_task();
}
