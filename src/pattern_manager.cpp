#include "pattern_manager.h"
#include "Log.h"
#include <LittleFS.h>
#include "Log.h"

bool pm_init() {
    if (!LittleFS.begin(true)) {
        Log.println(F("[PM] LittleFS 挂载失败"));
        return false;
    }
    Log.println(F("[PM] LittleFS 已就绪"));

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


