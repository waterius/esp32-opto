// Ядро: мелкая работа с текстом, общая для порта и тестов.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace core {

// Длина префикса, не обрывающего символ UTF-8 посреди байтов. Нужна там, где
// текст режется по границе буфера: лог отдаётся страницей по кусочкам, и
// половинка кириллической буквы на стыке ломает разбор на странице.
inline size_t utf8Trim(const char* text, size_t len) {
    size_t start = len;
    while (start > 0) {
        uint8_t c = (uint8_t)text[start - 1];
        if ((c & 0xC0) == 0x80) {  // байт-продолжение, ищем начало символа
            --start;
            continue;
        }
        size_t need = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
        return start - 1 + need <= len ? len : start - 1;
    }
    return len;  // сплошные байты-продолжения: не наше дело их чинить
}

// Обрезать строку на месте по границе символа UTF-8. Для сообщений об ошибках,
// которые собираются в буфер фиксированного размера и уходят прямо на страницу.
inline void utf8Truncate(char* text) {
    size_t len = 0;
    while (text[len]) ++len;
    text[utf8Trim(text, len)] = 0;
}

}  // namespace core
