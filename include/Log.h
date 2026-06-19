#ifndef LOG_H
#define LOG_H

#include <Arduino.h>
#include <Print.h>

class LogClass : public Print {
public:
    LogClass() {}

    void begin();
    void handle();

    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;

    using Print::printf;
};

extern LogClass Log;

#endif
