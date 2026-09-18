#include "log.h"

LogSink Log;

void LogSink::begin(unsigned long baud) {
    Serial.begin(baud);
    bootId_ = esp_random();
    mutex_ = xSemaphoreCreateMutex();
}

size_t LogSink::write(const uint8_t* data, size_t len) {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    size_t i = 0;
    while (i < len) {
        if (lineStart_) {
            // Каждая строка начинается со времени от старта: [секунды.миллисекунды]
            char stamp[20];
            unsigned long ms = millis();
            int n = snprintf(stamp, sizeof(stamp), "[%lu.%03lu] ", ms / 1000, ms % 1000);
            put((const uint8_t*)stamp, n);
            lineStart_ = false;
        }
        const uint8_t* nl = (const uint8_t*)memchr(data + i, '\n', len - i);
        size_t n = nl ? (size_t)(nl - data) + 1 - i : len - i;
        put(data + i, n);
        lineStart_ = nl != nullptr;
        i += n;
    }
    if (mutex_) xSemaphoreGive(mutex_);
    return len;
}

void LogSink::put(const uint8_t* data, size_t len) {
    Serial.write(data, len);
    while (len) {
        size_t pos = total_ % SIZE;
        size_t chunk = min(len, SIZE - pos);
        memcpy(buf_ + pos, data, chunk);
        total_ += chunk;
        data += chunk;
        len -= chunk;
    }
}

uint32_t LogSink::read(uint32_t from, char* out, size_t cap, size_t& len, bool& skipped) {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    uint32_t oldest = total_ > SIZE ? total_ - SIZE : 0;
    if (from > total_) from = 0;  // позиция из прошлой загрузки
    skipped = false;
    uint32_t pos = from;
    if (pos < oldest) {
        skipped = from > 0;
        // Старейшая строка обрезана буфером (возможно, посреди буквы UTF-8)
        for (pos = oldest; pos < total_ && buf_[pos % SIZE] != '\n';) ++pos;
        if (pos < total_) ++pos;
    }
    len = 0;
    while (pos < total_ && len < cap) out[len++] = buf_[pos++ % SIZE];
    if (mutex_) xSemaphoreGive(mutex_);
    return pos;
}
