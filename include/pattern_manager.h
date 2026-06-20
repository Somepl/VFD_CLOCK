#ifndef PATTERN_MANAGER_H
#define PATTERN_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

#define PM_PATTERNS_FILE "/patterns.json"
#define PM_ANIMATIONS_FILE "/animations.json"
#define PM_MAX_PATTERNS 20
#define PM_MAX_ANIMATIONS 10
#define PM_MAX_FRAMES 30

bool pm_init();
bool pm_load(JsonDocument &doc, const char *file);
bool pm_save(JsonDocument &doc, const char *file);

bool pm_load_patterns(JsonDocument &doc);
bool pm_save_patterns(JsonDocument &doc);

bool pm_load_animations(JsonDocument &doc);
bool pm_save_animations(JsonDocument &doc);

// 内置动画 override 存储（Preferences）
bool pm_get_builtin_override(uint8_t idx, JsonDocument &doc);
bool pm_set_builtin_override(uint8_t idx, JsonDocument &doc);
bool pm_delete_builtin_override(uint8_t idx);
bool pm_has_builtin_override(uint8_t idx);

#endif
