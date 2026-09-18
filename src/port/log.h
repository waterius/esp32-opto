// Порт: лог прошивки. Пишет в USB (Serial) и в кольцевой буфер, который
// показывает страница log.html. Вызывается из любой задачи: запись под мьютексом.
#pragma once
#include <Arduino.h>

class LogSink : public Print {
   public:
    static constexpr size_t SIZE = 16384;

    // Вместо Serial.begin(): до вызова лог не защищён мьютексом.
    void begin(unsigned long baud);

    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* data, size_t len) override;

    // Копирует в out текст, записанный после позиции from (сквозной счёт байт с
    // момента старта), и возвращает позицию для следующего запроса. Если часть
    // уже затёрта, skipped = true и копирование начинается с первой целой строки.
    uint32_t read(uint32_t from, char* out, size_t cap, size_t& len, bool& skipped);

    // Меняется при каждой загрузке: так страница узнаёт о перезагрузке.
    uint32_t bootId() const { return bootId_; }

   private:
    void put(const uint8_t* data, size_t len);

    char buf_[SIZE];
    uint32_t total_ = 0;  // сколько байт записано с момента старта
    bool lineStart_ = true;
    uint32_t bootId_ = 0;
    SemaphoreHandle_t mutex_ = nullptr;
};

extern LogSink Log;
