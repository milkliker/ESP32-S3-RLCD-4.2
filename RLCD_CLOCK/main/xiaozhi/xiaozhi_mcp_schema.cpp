// 构建小智 MCP initialize 响应和工具 JSON schema，不执行具体工具业务。
#include "xiaozhi_mcp_schema.h"
#include "xiaozhi_mcp_json.h"

#include <stdint.h>

namespace xiaozhi_mcp_schema {

const char kDeviceStatusTool[] = "self.get_device_status";
const char kSetVolumeTool[] = "self.audio_speaker.set_volume";
const char kSetAlarmTool[] = "self.alarm.set";
const char kDisableAlarmTool[] = "self.alarm.disable";
const char kAlarmRulesTool[] = "self.alarm.rules";
const char kSetCountdownTool[] = "self.timer.set_countdown";
const char kPomodoroControlTool[] = "self.pomodoro.control";
const char kSetWeatherCityTool[] = "self.weather.set_city";

namespace {
constexpr const char *kMcpProtocolVersion = "2024-11-05";
constexpr const char *kMcpServerName = "ESP32-S3-RLCD-4.2";
constexpr const char *kJsonFieldType = "type";
constexpr const char *kJsonFieldProperties = "properties";
constexpr const char *kJsonFieldRequired = "required";
constexpr const char *kJsonFieldAction = "action";
constexpr const char *kJsonFieldDurationSeconds = "duration_seconds";
constexpr const char *kJsonTypeObject = "object";
constexpr const char *kJsonTypeString = "string";
constexpr const char *kJsonTypeInteger = "integer";
constexpr const char *kJsonTypeBoolean = "boolean";
constexpr const char *kPomodoroActionStart = "start";
constexpr const char *kAlarmRulesActionSet = "set";
constexpr const char *kAlarmRulesActionList = "list";
constexpr const char *kAlarmRulesActionToggle = "toggle";
constexpr const char *kAlarmRulesActionRemove = "remove";
constexpr const char *kPomodoroActionCancel = "cancel";
constexpr const char *kPomodoroActionStatus = "status";
constexpr const char *kDeviceStatusDescription =
    "Get local temperature, humidity, battery percentage and speaker volume.";
constexpr const char *kSetVolumeDescription =
    "Set the device speaker volume from 0 to 100 percent.";
constexpr const char *kSetAlarmDescription =
    "Set the one-shot alarm at the next occurrence of the supplied 24-hour local hour and minute. If another alarm already exists at a different time, the first call MUST omit confirm_replace or set it to false. The device then returns confirmation_required with the existing and requested times; ask the user whether to replace it. Only after the user explicitly confirms may you call the same requested time again with confirm_replace=true. Never set confirm_replace=true on the initial request. Setting the same time is idempotent and needs no confirmation.";
constexpr const char *kDisableAlarmDescription =
    "Disable or stop the local alarm.";
constexpr const char *kSetCountdownDescription =
    "Set a local countdown reminder in seconds. Never use this tool for requests containing focus, 专注, or 番茄钟; those requests MUST use self.pomodoro.control.";
constexpr const char *kPomodoroControlDescription =
    "Control the focus Pomodoro timer. You MUST call this tool for every request containing focus, 专注, or 番茄钟, and MUST NOT claim that a timer started or changed unless the tool returned success. "
    "Use start to create or replace it, cancel to stop it, and status to query it. "
    "Do not use alarm tools for focus or Pomodoro requests, and do not use this tool for ordinary reminders.";
constexpr const char *kSetWeatherCityDescription =
    "Set the QWeather location from the user's spoken city name. Pass the city exactly as spoken, including an optional Chinese 市 suffix; the device normalizes and validates a manual city with QWeather before saving. To restore IP-based automatic location, call this same tool with city=自动. Never claim the location changed unless the tool returned success.";
constexpr const char *kAlarmRulesDescription =
    "Manage repeating alarms in addition to the one-shot alarm. Use for any request containing 每天, 每周, 周一 to 周日, 工作日, 每月, 农历 repeats. "
    "action=set creates an enabled repeating rule and requires hour and minute plus a recur value: 0=once(same as self.alarm.set but keeps label), 1=daily, 2=weekly (dow_mask bitmask: bit0=Sunday..bit6=Saturday, combine multiple bits; 0x7C means Monday to Friday workdays), 3=monthly (mday 1-31; if a month lacks the day it fires on the month end), 4=monthly_nth (nth 1-4 plus weekday 0=Sunday..6=Saturday; nth=5 means the last such weekday of the month), 5=lunar_monthly (lunar_day 1-30 by the Chinese lunar calendar; fires on month end when the lunar month is short). "
    "action=list returns all rules with their id, time, repeat kind and enabled state; call it first whenever the user refers to an alarm by description rather than id, or asks what alarms exist. "
    "action=toggle switches one rule on or off by id (enabled=true to enable). "
    "action=remove deletes one rule by id. "
    "Never claim an alarm was created or changed unless the tool returned success.";
constexpr int kHoursPerDay = 24;
constexpr int kMinutesPerHour = 60;
constexpr uint32_t kMaxCountdownSeconds = 7U * 24U * 60U * 60U;
constexpr uint32_t kMaxPomodoroSeconds = 99U * 60U + 59U;

using xiaozhi_mcp_json::add_string;
using xiaozhi_mcp_json::add_owned_item_to_array;
using xiaozhi_mcp_json::add_owned_item_to_object;

cJSON *create_object_schema()
{
    cJSON *schema = cJSON_CreateObject();
    if (!schema || !add_string(schema, kJsonFieldType, kJsonTypeObject) ||
        !add_owned_item_to_object(schema,
                                  kJsonFieldProperties,
                                  cJSON_CreateObject())) {
        cJSON_Delete(schema);
        return nullptr;
    }
    return schema;
}

cJSON *create_tool(const char *name, const char *description, cJSON *input_schema)
{
    if (!input_schema) {
        return nullptr;
    }
    cJSON *tool = cJSON_CreateObject();
    if (!tool || !add_string(tool, "name", name) ||
        !add_string(tool, "description", description)) {
        cJSON_Delete(tool);
        cJSON_Delete(input_schema);
        return nullptr;
    }
    if (!add_owned_item_to_object(tool, "inputSchema", input_schema)) {
        cJSON_Delete(tool);
        return nullptr;
    }
    return tool;
}

cJSON *get_or_create_required_array(cJSON *schema)
{
    cJSON *required = schema ? cJSON_GetObjectItem(schema, kJsonFieldRequired) : nullptr;
    if (required) {
        return cJSON_IsArray(required) ? required : nullptr;
    }
    required = cJSON_CreateArray();
    if (!add_owned_item_to_object(schema, kJsonFieldRequired, required)) {
        return nullptr;
    }
    return required;
}

bool add_required_integer(cJSON *schema,
                          const char *name,
                          int minimum,
                          int maximum)
{
    cJSON *properties = schema ? cJSON_GetObjectItem(schema, kJsonFieldProperties) : nullptr;
    cJSON *property = cJSON_CreateObject();
    cJSON *required = get_or_create_required_array(schema);
    if (!cJSON_IsObject(properties) || !property || !cJSON_IsArray(required) ||
        !add_string(property, kJsonFieldType, kJsonTypeInteger) ||
        !cJSON_AddNumberToObject(property, "minimum", minimum) ||
        !cJSON_AddNumberToObject(property, "maximum", maximum) ||
        !add_owned_item_to_array(required, cJSON_CreateString(name))) {
        cJSON_Delete(property);
        return false;
    }
    return add_owned_item_to_object(properties, name, property);
}

bool add_optional_string(cJSON *schema, const char *name)
{
    cJSON *properties = schema ? cJSON_GetObjectItem(schema, kJsonFieldProperties) : nullptr;
    cJSON *property = cJSON_CreateObject();
    if (!cJSON_IsObject(properties) || !property ||
        !add_string(property, kJsonFieldType, kJsonTypeString)) {
        cJSON_Delete(property);
        return false;
    }
    return add_owned_item_to_object(properties, name, property);
}

bool add_optional_boolean(cJSON *schema, const char *name)
{
    cJSON *properties = schema ? cJSON_GetObjectItem(schema, kJsonFieldProperties) : nullptr;
    cJSON *property = cJSON_CreateObject();
    if (!cJSON_IsObject(properties) || !property ||
        !add_string(property, kJsonFieldType, kJsonTypeBoolean)) {
        cJSON_Delete(property);
        return false;
    }
    return add_owned_item_to_object(properties, name, property);
}

bool add_required_string(cJSON *schema, const char *name)
{
    cJSON *properties = schema ? cJSON_GetObjectItem(schema, kJsonFieldProperties) : nullptr;
    cJSON *property = cJSON_CreateObject();
    cJSON *required = get_or_create_required_array(schema);
    if (!cJSON_IsObject(properties) || !property || !cJSON_IsArray(required) ||
        !add_string(property, kJsonFieldType, kJsonTypeString) ||
        !add_owned_item_to_array(required, cJSON_CreateString(name))) {
        cJSON_Delete(property);
        return false;
    }
    return add_owned_item_to_object(properties, name, property);
}

bool add_optional_integer(cJSON *schema, const char *name, int minimum, int maximum)
{
    cJSON *properties = schema ? cJSON_GetObjectItem(schema, kJsonFieldProperties) : nullptr;
    cJSON *property = cJSON_CreateObject();
    if (!cJSON_IsObject(properties) || !property ||
        !add_string(property, kJsonFieldType, kJsonTypeInteger) ||
        !cJSON_AddNumberToObject(property, "minimum", minimum) ||
        !cJSON_AddNumberToObject(property, "maximum", maximum)) {
        cJSON_Delete(property);
        return false;
    }
    return add_owned_item_to_object(properties, name, property);
}

bool add_required_action(cJSON *schema)
{
    cJSON *properties = schema ? cJSON_GetObjectItem(schema, kJsonFieldProperties) : nullptr;
    cJSON *property = cJSON_CreateObject();
    cJSON *values = cJSON_CreateArray();
    cJSON *required = cJSON_CreateArray();
    if (!cJSON_IsObject(properties) || !property || !values || !required ||
        !add_string(property, kJsonFieldType, kJsonTypeString) ||
        !add_owned_item_to_array(values, cJSON_CreateString(kPomodoroActionStart)) ||
        !add_owned_item_to_array(values, cJSON_CreateString(kPomodoroActionCancel)) ||
        !add_owned_item_to_array(values, cJSON_CreateString(kPomodoroActionStatus)) ||
        !add_owned_item_to_array(required, cJSON_CreateString(kJsonFieldAction))) {
        cJSON_Delete(property);
        cJSON_Delete(values);
        cJSON_Delete(required);
        return false;
    }
    if (!add_owned_item_to_object(property, "enum", values)) {
        cJSON_Delete(property);
        cJSON_Delete(required);
        return false;
    }
    if (!add_owned_item_to_object(properties, kJsonFieldAction, property)) {
        cJSON_Delete(required);
        return false;
    }
    return add_owned_item_to_object(schema, kJsonFieldRequired, required);
}

cJSON *create_integer_tool(const char *name,
                           const char *description,
                           const char *argument,
                           int minimum,
                           int maximum)
{
    cJSON *schema = create_object_schema();
    if (!schema || !add_required_integer(schema, argument, minimum, maximum)) {
        cJSON_Delete(schema);
        return nullptr;
    }
    return create_tool(name, description, schema);
}

cJSON *create_alarm_tool()
{
    cJSON *schema = create_object_schema();
    if (!schema ||
        !add_required_integer(schema, "hour", 0, kHoursPerDay - 1) ||
        !add_required_integer(schema, "minute", 0, kMinutesPerHour - 1) ||
        !add_optional_boolean(schema, "confirm_replace") ||
        !add_optional_string(schema, "label")) {
        cJSON_Delete(schema);
        return nullptr;
    }
    return create_tool(kSetAlarmTool, kSetAlarmDescription, schema);
}

cJSON *create_countdown_tool()
{
    cJSON *schema = create_object_schema();
    if (!schema ||
        !add_required_integer(schema,
                              kJsonFieldDurationSeconds,
                              1,
                              static_cast<int>(kMaxCountdownSeconds)) ||
        !add_optional_string(schema, "label")) {
        cJSON_Delete(schema);
        return nullptr;
    }
    return create_tool(kSetCountdownTool, kSetCountdownDescription, schema);
}

cJSON *create_pomodoro_tool()
{
    cJSON *schema = create_object_schema();
    if (!schema || !add_required_action(schema) ||
        !add_optional_integer(schema,
                              kJsonFieldDurationSeconds,
                              1,
                              static_cast<int>(kMaxPomodoroSeconds))) {
        cJSON_Delete(schema);
        return nullptr;
    }
    return create_tool(kPomodoroControlTool, kPomodoroControlDescription, schema);
}

cJSON *create_weather_city_tool()
{
    cJSON *schema = create_object_schema();
    if (!schema || !add_required_string(schema, "city")) {
        cJSON_Delete(schema);
        return nullptr;
    }
    return create_tool(kSetWeatherCityTool, kSetWeatherCityDescription, schema);
}

cJSON *create_alarm_rules_tool()
{
    cJSON *schema = create_object_schema();
    if (!schema) {
        return nullptr;
    }
    // action 为必选枚举
    cJSON *properties = cJSON_GetObjectItem(schema, kJsonFieldProperties);
    cJSON *action = cJSON_CreateObject();
    cJSON *values = cJSON_CreateArray();
    cJSON *required = cJSON_CreateArray();
    if (!cJSON_IsObject(properties) || !action || !values || !required ||
        !add_string(action, kJsonFieldType, kJsonTypeString) ||
        !add_owned_item_to_array(values, cJSON_CreateString(kAlarmRulesActionSet)) ||
        !add_owned_item_to_array(values, cJSON_CreateString(kAlarmRulesActionList)) ||
        !add_owned_item_to_array(values, cJSON_CreateString(kAlarmRulesActionToggle)) ||
        !add_owned_item_to_array(values, cJSON_CreateString(kAlarmRulesActionRemove)) ||
        !add_owned_item_to_object(action, "enum", values) ||
        !add_owned_item_to_object(properties, kJsonFieldAction, action) ||
        !add_owned_item_to_array(required, cJSON_CreateString(kJsonFieldAction))) {
        cJSON_Delete(action);
        cJSON_Delete(values);
        cJSON_Delete(required);
        cJSON_Delete(schema);
        return nullptr;
    }
    if (!add_optional_integer(schema, "id", 1, 255) ||
        !add_optional_integer(schema, "hour", 0, kHoursPerDay - 1) ||
        !add_optional_integer(schema, "minute", 0, kMinutesPerHour - 1) ||
        !add_optional_integer(schema, "recur", 0, 5) ||
        !add_optional_integer(schema, "dow_mask", 1, 127) ||
        !add_optional_integer(schema, "mday", 1, 31) ||
        !add_optional_integer(schema, "nth", 1, 5) ||
        !add_optional_integer(schema, "weekday", 0, 6) ||
        !add_optional_integer(schema, "lunar_day", 1, 30) ||
        !add_optional_boolean(schema, "enabled") ||
        !add_optional_string(schema, "label")) {
        cJSON_Delete(schema);
        return nullptr;
    }
    return create_tool(kAlarmRulesTool, kAlarmRulesDescription, schema);
}

bool add_tool_if_present(cJSON *tools, cJSON *tool)
{
    return add_owned_item_to_array(tools, tool);
}
} // namespace

cJSON *create_tools_list_result(bool alarm_enabled,
                                bool alarm_disable_enabled,
                                bool alarm_rules_enabled,
                                bool countdown_enabled,
                                bool pomodoro_enabled,
                                bool weather_city_enabled)
{
    cJSON *result = cJSON_CreateObject();
    if (!result) {
        return nullptr;
    }
    cJSON *tools = cJSON_CreateArray();
    if (!tools) {
        cJSON_Delete(result);
        return nullptr;
    }
    if (!add_tool_if_present(tools, create_tool(kDeviceStatusTool,
                                                kDeviceStatusDescription,
                                                create_object_schema())) ||
        !add_tool_if_present(tools, create_integer_tool(kSetVolumeTool,
                                                        kSetVolumeDescription,
                                                        "volume",
                                                        0,
                                                        100)) ||
        (alarm_enabled && !add_tool_if_present(tools, create_alarm_tool())) ||
        (alarm_disable_enabled &&
         !add_tool_if_present(tools, create_tool(kDisableAlarmTool,
                                                 kDisableAlarmDescription,
                                                 create_object_schema()))) ||
        (alarm_rules_enabled &&
         !add_tool_if_present(tools, create_alarm_rules_tool())) ||
        (countdown_enabled && !add_tool_if_present(tools, create_countdown_tool())) ||
        (pomodoro_enabled && !add_tool_if_present(tools, create_pomodoro_tool())) ||
        (weather_city_enabled && !add_tool_if_present(tools, create_weather_city_tool()))) {
        cJSON_Delete(tools);
        cJSON_Delete(result);
        return nullptr;
    }
    if (!add_owned_item_to_object(result, "tools", tools)) {
        cJSON_Delete(result);
        return nullptr;
    }
    return result;
}

cJSON *create_initialize_result(const char *app_version)
{
    cJSON *result = cJSON_CreateObject();
    if (!result) {
        return nullptr;
    }
    cJSON *capabilities = cJSON_CreateObject();
    if (!capabilities) {
        cJSON_Delete(result);
        return nullptr;
    }
    cJSON *tools = cJSON_CreateObject();
    if (!tools) {
        cJSON_Delete(capabilities);
        cJSON_Delete(result);
        return nullptr;
    }
    cJSON *server_info = cJSON_CreateObject();
    if (!server_info) {
        cJSON_Delete(tools);
        cJSON_Delete(capabilities);
        cJSON_Delete(result);
        return nullptr;
    }
    if (!add_string(result, "protocolVersion", kMcpProtocolVersion) ||
        !add_string(server_info, "name", kMcpServerName) ||
        !add_string(server_info, "version", app_version)) {
        cJSON_Delete(result);
        cJSON_Delete(capabilities);
        cJSON_Delete(tools);
        cJSON_Delete(server_info);
        return nullptr;
    }
    if (!add_owned_item_to_object(capabilities, "tools", tools)) {
        cJSON_Delete(result);
        cJSON_Delete(capabilities);
        cJSON_Delete(server_info);
        return nullptr;
    }
    if (!add_owned_item_to_object(result, "capabilities", capabilities)) {
        cJSON_Delete(result);
        cJSON_Delete(server_info);
        return nullptr;
    }
    if (!add_owned_item_to_object(result, "serverInfo", server_info)) {
        cJSON_Delete(result);
        return nullptr;
    }
    return result;
}

} // namespace xiaozhi_mcp_schema
