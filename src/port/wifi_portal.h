// Порт: настройка Wi-Fi — перенос нужной части портала waterius
// (ESP8266/src/portal/active_point*.cpp): скан сетей, подключение, статус,
// captive portal в режиме точки доступа.
#pragma once
#include <ESPAsyncWebServer.h>

namespace wifi_portal {

void registerRoutes(AsyncWebServer& server);
// DNS-перехват при поднятой AP и применение новой сети из формы.
void loop();

}  // namespace wifi_portal
