// Ядро: кому отдавать сообщение лога.
//
// Приёмников два, и требования к ним прямо противоположные. В USB-serial место
// не кончается: туда полезно слать всё, включая строку состояния раз в пять
// секунд. Кольцевой буфер страницы лога — 16 КБ, примерно 250 строк, и если
// набивать его той же подробностью, то к моменту, когда понадобится разобрать
// отказ, всё интересное из него уже вытеснится.
//
// Поэтому порога два, независимых, и решает их сравнение только этот модуль:
// порт знает, куда писать байты, но не знает, какие.
#pragma once
#include <stdint.h>

namespace core {

// По возрастанию подробности: чем больше значение, тем мельче событие.
enum class LogLevel : uint8_t { Error = 0, Warn, Info, Debug, Trace };

enum class LogRoute : uint8_t { None, SerialOnly, BufferOnly, Both };

struct LogLevels {
    LogLevel serial = LogLevel::Trace;  // в порт идёт всё
    LogLevel buffer = LogLevel::Debug;  // trace на страницу не попадает
};

// Приёмник берёт сообщение, если оно не подробнее его порога.
inline bool accepts(LogLevel threshold, LogLevel msg) {
    return (uint8_t)msg <= (uint8_t)threshold;
}

inline LogRoute route(LogLevel msg, const LogLevels& levels) {
    bool serial = accepts(levels.serial, msg);
    bool buffer = accepts(levels.buffer, msg);
    if (serial && buffer) return LogRoute::Both;
    if (serial) return LogRoute::SerialOnly;
    if (buffer) return LogRoute::BufferOnly;
    return LogRoute::None;
}

// Латиницей: имя уходит в строку о режимах лога, которую грепают скриптом.
inline const char* levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return "error";
        case LogLevel::Warn: return "warn";
        case LogLevel::Info: return "info";
        case LogLevel::Debug: return "debug";
        case LogLevel::Trace: return "trace";
    }
    return "?";  // значение из будущей версии прошивки
}

}  // namespace core
