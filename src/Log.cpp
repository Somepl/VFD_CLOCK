#include "Log.h"

LogClass Log;

void LogClass::begin() {
}

void LogClass::handle() {
}

size_t LogClass::write(uint8_t c) {
    return Serial.write(c);
}

size_t LogClass::write(const uint8_t *buffer, size_t size) {
    return Serial.write(buffer, size);
}
