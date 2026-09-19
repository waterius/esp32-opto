// Порт: лог прошивки. Пишет в USB (Serial) и в кольцевой буфер, который
// показывает страница log.html. Вызывается из любой задачи: запись под мьютексом.
//
// У приёмников независимые пороги (core/log_level.h): в порт по умолчанию идёт
// всё, включая строку состояния, а в буфер — всё, кроме trace. Так страница
// лога остаётся читаемой, а по USB видно подробности.
//
// Единица записи — целая строка, а не поток байт. Иначе println(), который
// Print разбивает на два вызова write(), разъехался бы: между вызовами мьютекс
// отпускается, и сообщение из другой задачи (события Wi-Fi, async_tcp) успело
// бы вклиниться в середину — в порт склеенным, а в буфер отдельной пустой
// строкой. Поэтому незакрытая строка копится в pending_ и уходит целиком.
#pragma once
#include <Arduino.h>

#include "core/log_level.h"

class LogSink : public Print {
   public:
    static constexpr size_t SIZE = 16384;
    // Самая длинная наша строка — ошибка счётчика с обвязкой, около 155 байт.
    // Больше класть нельзя: буфер живёт на стеке задачи, а у сервера RFC 2217
    // он 4 КБ. Строка состояния длиннее и идёт мимо, через line().
    static constexpr size_t LINE_CAP = 256;

    // Вместо Serial.begin(): до вызова лог не защищён мьютексом.
    void begin(unsigned long baud);

    void setLevels(const core::LogLevels& levels) { levels_ = levels; }
    const core::LogLevels& levels() const { return levels_; }

    // Прежде чем собирать факты для строки состояния, стоит спросить, нужны ли они.
    bool enabled(core::LogLevel level) const {
        return core::route(level, levels_) != core::LogRoute::None;
    }

    void log(core::LogLevel level, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
    void error(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void warn(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void info(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void debug(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void trace(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    // Готовая строка без перевода строки: для строки состояния, которая длиннее
    // LINE_CAP и уже собрана в своём буфере — копировать её ещё раз незачем.
    void line(core::LogLevel level, const char* text, size_t len);

    // Унаследованные от Print println/printf остаются рабочими и считаются info.
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* data, size_t len) override;

    // Копирует в out текст, записанный после позиции from (сквозной счёт байт с
    // момента старта), и возвращает позицию для следующего запроса. Если часть
    // уже затёрта, skipped = true и копирование начинается с первой целой строки.
    uint32_t read(uint32_t from, char* out, size_t cap, size_t& len, bool& skipped);

    // Меняется при каждой загрузке: так страница узнаёт о перезагрузке.
    uint32_t bootId() const { return bootId_; }

   private:
    void lock();
    void unlock();
    void vlog(core::LogLevel level, const char* fmt, va_list args);
    // Всё ниже — только под мьютексом.
    void record(core::LogLevel level, const char* text, size_t len);
    void flushPending();
    void put(core::LogRoute route, const uint8_t* data, size_t len);

    char buf_[SIZE];
    uint32_t total_ = 0;  // сколько байт принял буфер с момента старта
    uint32_t bootId_ = 0;
    core::LogLevels levels_;
    SemaphoreHandle_t mutex_ = nullptr;

    // Незакрытая строка, пришедшая унаследованным путём Print
    char pending_[LINE_CAP];
    size_t pendingLen_ = 0;
    TaskHandle_t pendingOwner_ = nullptr;
};

extern LogSink Log;
