#include "log.h"

#include "core/text.h"

LogSink Log;

void LogSink::begin(unsigned long baud) {
    // Кольцо передатчика создаётся внутри begin() и только если его ещё нет,
    // поэтому размер задаём раньше: по умолчанию там 256 байт, и строка
    // состояния при неподключённом хосте молча резалась бы пополам.
    Serial.setTxBufferSize(1024);
    Serial.begin(baud);
    // Не ноль! При нуле HWCDC::write() с забитым кольцом уходит в вечный цикл
    // delay(1): отправка нуля байт в byte-буфер считается успехом, прогресса
    // нет, счётчик попыток уходит в переполнение, и выйти можно только выдернув
    // кабель — то есть открытый и не вычитываемый терминал ронял бы плату по
    // сторожу цикла. Единица — это один тик (CONFIG_FREERTOS_HZ = 1000), после
    // которого драйвер сам считает хост отпавшим.
    Serial.setTxTimeoutMs(1);
    // Дубль в UART0. Когда плата не на USB — на аккумуляторе, в щитке, у
    // счётчика — снять лог нечем: Serial у нас USB-Serial/JTAG, и без хоста
    // всё напечатанное выбрасывается. На ножках UART0 (у C3 GPIO21 — TX, у S3
    // GPIO43) лог можно читать переходником USB-TTL в любой момент. Порт там
    // всё равно занят: туда, мимо наших порогов, пишет отладка ядра Arduino.
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.setTxBufferSize(1024);  // как и у Serial — до begin()
    Serial0.begin(baud);
#endif
    bootId_ = esp_random();
    mutex_ = xSemaphoreCreateMutex();
}

void LogSink::lock() {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
}

void LogSink::unlock() {
    if (mutex_) xSemaphoreGive(mutex_);
}

#define LOG_LEVEL_METHOD(name, level)             \
    void LogSink::name(const char* fmt, ...) {    \
        va_list args;                             \
        va_start(args, fmt);                      \
        vlog(core::LogLevel::level, fmt, args);   \
        va_end(args);                             \
    }

LOG_LEVEL_METHOD(error, Error)
LOG_LEVEL_METHOD(warn, Warn)
LOG_LEVEL_METHOD(info, Info)
LOG_LEVEL_METHOD(debug, Debug)
LOG_LEVEL_METHOD(trace, Trace)

#undef LOG_LEVEL_METHOD

void LogSink::vlog(core::LogLevel level, const char* fmt, va_list args) {
    if (!enabled(level)) return;  // формат не разбираем зря
    char text[LINE_CAP];
    int written = vsnprintf(text, sizeof(text), fmt, args);
    if (written < 0) return;
    if ((size_t)written >= sizeof(text)) {
        // Обрезанное не должно выглядеть целым, а буква — рваться пополам
        text[sizeof(text) - 4] = 0;
        core::utf8Truncate(text);
        strcat(text, "…");
    }
    lock();
    flushPending();
    record(level, text, strlen(text));
    unlock();
}

void LogSink::line(core::LogLevel level, const char* text, size_t len) {
    if (!enabled(level)) return;
    lock();
    flushPending();
    record(level, text, len);
    unlock();
}

size_t LogSink::write(const uint8_t* data, size_t len) {
    lock();
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    // Чужая незакрытая строка: выталкиваем её, чтобы не склеить с нашей
    if (pendingLen_ && pendingOwner_ != self) flushPending();
    pendingOwner_ = self;
    for (size_t i = 0; i < len; ++i) {
        char c = (char)data[i];
        if (c == '\n') {
            flushPending();  // пустые строки не печатаем: один штамп без текста бесполезен
            continue;
        }
        if (c == '\r') continue;  // перевод строки ставим сами
        if (pendingLen_ + 1 >= LINE_CAP) flushPending();
        pending_[pendingLen_++] = c;
    }
    unlock();
    return len;
}

void LogSink::flushPending() {
    if (!pendingLen_) return;
    size_t len = pendingLen_;
    pendingLen_ = 0;
    record(core::LogLevel::Info, pending_, len);
}

void LogSink::record(core::LogLevel level, const char* text, size_t len) {
    core::LogRoute route = core::route(level, levels_);
    if (route == core::LogRoute::None) return;
    while (len && (text[len - 1] == '\n' || text[len - 1] == '\r')) --len;

    // Каждая строка начинается со времени от старта: [секунды.миллисекунды]
    char stamp[20];
    unsigned long ms = millis();
    int n = snprintf(stamp, sizeof(stamp), "[%lu.%03lu] ", ms / 1000, ms % 1000);
    put(route, (const uint8_t*)stamp, n);
    put(route, (const uint8_t*)text, len);
    put(route, (const uint8_t*)"\n", 1);
}

void LogSink::put(core::LogRoute route, const uint8_t* data, size_t len) {
    if (route == core::LogRoute::SerialOnly || route == core::LogRoute::Both) {
        Serial.write(data, len);
#if ARDUINO_USB_CDC_ON_BOOT
        // Дубль по остаточному принципу: UART0 на 115200 отдаёт 11 КБ/с, а
        // байты прозрачной сессии идут быстрее. Не влезло — выбрасываем кусок
        // целиком. Ждать освобождения кольца нельзя: это ровно та ошибка, из-за
        // которой setTxTimeoutMs(0) ронял плату по сторожу цикла.
        if (Serial0.availableForWrite() >= (int)len) Serial0.write(data, len);
#endif
    }
    if (route != core::LogRoute::BufferOnly && route != core::LogRoute::Both) return;
    // total_ растёт только на принятых буфером байтах: иначе позиции from,
    // которыми страница лога дочитывает остаток, показывали бы в пустоту
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
    lock();
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
    unlock();
    return pos;
}
