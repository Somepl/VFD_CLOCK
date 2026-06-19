#include "pattern_manager.h"
#include <LittleFS.h>
#include <Preferences.h>

#define PM_BUILTIN_NS "builtin"

bool pm_init() {
    if (!LittleFS.begin(true)) {
        Serial.println(F("[PM] LittleFS 挂载失败"));
        return false;
    }
    Serial.println(F("[PM] LittleFS 已就绪"));

    if (!LittleFS.exists(PM_PATTERNS_FILE)) {
        File f = LittleFS.open(PM_PATTERNS_FILE, "w");
        if (f) {
            f.print("[]");
            f.close();
        }
    }
    if (!LittleFS.exists(PM_ANIMATIONS_FILE)) {
        File f = LittleFS.open(PM_ANIMATIONS_FILE, "w");
        if (f) {
            f.print("[]");
            f.close();
        }
    }
    return true;
}

bool pm_load(JsonDocument &doc, const char *file) {
    if (!LittleFS.exists(file)) return false;
    File f = LittleFS.open(file, "r");
    if (!f) return false;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    return (err == DeserializationError::Ok);
}

bool pm_save(JsonDocument &doc, const char *file) {
    File f = LittleFS.open(file, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
}

bool pm_load_patterns(JsonDocument &doc) {
    return pm_load(doc, PM_PATTERNS_FILE);
}

bool pm_save_patterns(JsonDocument &doc) {
    return pm_save(doc, PM_PATTERNS_FILE);
}

bool pm_load_animations(JsonDocument &doc) {
    return pm_load(doc, PM_ANIMATIONS_FILE);
}

bool pm_save_animations(JsonDocument &doc) {
    return pm_save(doc, PM_ANIMATIONS_FILE);
}

bool pm_get_builtin_override(uint8_t idx, JsonDocument &doc) {
    Preferences prefs;
    prefs.begin(PM_BUILTIN_NS, true);
    String val = prefs.getString(("ov" + String(idx)).c_str(), "");
    prefs.end();
    if (val.length() == 0) return false;
    DeserializationError err = deserializeJson(doc, val);
    return (err == DeserializationError::Ok);
}

bool pm_set_builtin_override(uint8_t idx, JsonDocument &doc) {
    Preferences prefs;
    prefs.begin(PM_BUILTIN_NS, false);
    String val;
    serializeJson(doc, val);
    bool ok = prefs.putString(("ov" + String(idx)).c_str(), val);
    prefs.end();
    return ok;
}

bool pm_delete_builtin_override(uint8_t idx) {
    Preferences prefs;
    prefs.begin(PM_BUILTIN_NS, false);
    prefs.remove(("ov" + String(idx)).c_str());
    prefs.end();
    return true;
}
