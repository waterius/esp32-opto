// Порт: веб-сервер. Страницы — статика из LittleFS (data/), данные — JSON API.
// Обработчики выполняются в задаче async_tcp: они только читают состояние
// и ставят флаги, всё остальное делает loop().
#pragma once

namespace web {
void begin();
void loop();
}  // namespace web
