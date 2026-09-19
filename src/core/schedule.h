// Ядро: расписание выхода на связь с облаком.
//
// Две вещи, которых не хватало: разброс периода и рост паузы между повторами.
// Без разброса все устройства с этой прошивкой стучатся в облако в одну и ту же
// секунду — Tasmota по той же причине сеет разброс по идентификатору чипа
// (retry_init = WIFI_RETRY_OFFSET_SEC + (chipId & 0xF)). Без роста паузы сутки
// недоступного сервера стоят 288 бесполезных TLS-рукопожатий.
#pragma once
#include <stdint.h>

namespace core {

// Постоянный для устройства сдвиг в пределах spanMs. Соседние chipId должны
// давать непохожие сдвиги, поэтому биты перемешиваются.
inline uint32_t jitterMs(uint32_t seed, uint32_t spanMs) {
    if (!spanMs) return 0;
    uint32_t h = seed * 2654435761u;  // множитель Кнута
    h ^= h >> 16;
    return h % spanMs;
}

// Пауза перед попыткой номер attempt (1 — первая после отказа):
// baseMs, 2×baseMs, 4×baseMs… но не больше maxMs.
inline uint32_t retryDelayMs(uint8_t attempt, uint32_t baseMs, uint32_t maxMs) {
    if (!attempt) return 0;
    uint32_t delay = baseMs;
    for (uint8_t i = 1; i < attempt; ++i) {
        if (delay >= maxMs / 2) return maxMs;
        delay *= 2;
    }
    return delay > maxMs ? maxMs : delay;
}

}  // namespace core
