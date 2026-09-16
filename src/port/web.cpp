#include "web.h"

#include <WebServer.h>

#include "../app.h"
#include "net.h"

namespace web {
namespace {

WebServer server(80);

String opt(const char* value, const char* current, const char* label) {
    String s = "<option value='";
    s += value;
    s += "'";
    if (String(current) == value) s += " selected";
    s += ">";
    s += label;
    s += "</option>";
    return s;
}

String field(const char* name, const char* label, const String& value, const char* type = "text") {
    String s = "<label>";
    s += label;
    s += "<input name='";
    s += name;
    s += "' type='";
    s += type;
    s += "' value='";
    s += value;
    s += "'></label>";
    return s;
}

String page() {
    const Settings& st = app.sett;
    const core::MeterData& m = app.last;

    String h =
        "<!doctype html><html lang='ru'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>esp32-opto</title><style>"
        "body{font:16px system-ui;margin:0;padding:16px;max-width:560px}"
        "h1{font-size:20px}h2{font-size:16px;margin-top:24px}"
        "label{display:block;margin:8px 0}input,select{width:100%;padding:6px;font-size:16px;"
        "box-sizing:border-box}"
        "table{border-collapse:collapse;width:100%}td{padding:4px 0;border-bottom:1px solid #eee}"
        "button{padding:10px 16px;font-size:16px;margin-top:12px}"
        ".s{color:#666;font-size:14px}</style></head><body>";

    h += "<h1>esp32-opto</h1><p class='s'>Прошивка ";
    h += FIRMWARE_VERSION;
    h += ", IP ";
    h += net::ip();
    h += "</p>";

    h += "<h2>Счётчик</h2><table>";
    h += "<tr><td>Состояние</td><td>";
    h += m.valid ? "прочитан" : (m.error[0] ? String(m.error) : String("ещё не читали"));
    h += "</td></tr>";
    if (m.valid) {
        h += "<tr><td>Серийный номер</td><td>" + String(m.serial) + "</td></tr>";
        h += "<tr><td>Модель</td><td>" + String(m.model) + "</td></tr>";
        h += "<tr><td>Версия ПО счётчика</td><td>" + String(m.fwVersion) + "</td></tr>";
        h += "<tr><td>Время счётчика</td><td>" + String(m.time) + "</td></tr>";
        h += "<tr><td>Всего, кВт·ч</td><td>" + String(m.total, 3) + "</td></tr>";
        for (uint8_t i = 0; i < m.tariffCount; i++)
            h += "<tr><td>Тариф T" + String(i + 1) + ", кВт·ч</td><td>" + String(m.tariff[i], 3) +
                 "</td></tr>";
    }
    h += "<tr><td>Облако</td><td>";
    h += app.cloudStatus[0] ? app.cloudStatus : "ещё не отправляли";
    h += "</td></tr></table>";
    h += "<form method='post' action='/read'><button>Прочитать сейчас</button></form>";

    h += "<form method='post' action='/save'>";
    h += "<h2>Оптопорт</h2>";
    h += field("baud", "Скорость", String(st.serial.baud), "number");
    h += "<label>Биты данных<select name='bits'>";
    for (int b = 5; b <= 8; b++) {
        char v[4];
        snprintf(v, sizeof(v), "%d", b);
        h += opt(v, String(st.serial.bits).c_str(), v);
    }
    h += "</select></label>";
    h += "<label>Чётность<select name='parity'>";
    char par[2] = {st.serial.parity, 0};
    h += opt("N", par, "нет");
    h += opt("E", par, "чётность");
    h += opt("O", par, "нечётность");
    h += "</select></label>";
    h += "<label>Стоп-биты<select name='stop'>";
    h += opt("1", String(st.serial.stop).c_str(), "1");
    h += opt("2", String(st.serial.stop).c_str(), "2");
    h += "</select></label>";
    h += field("rfc", "Порт RFC 2217", String(st.rfcPort), "number");

    h += "<h2>Счётчик НАРТИС</h2>";
    h += field("addr", "Адрес (0 — определить самому)", String(st.meterAddr), "number");
    h += field("mpwd", "Пароль LLS", String(st.meterPwd));

    h += "<h2>Облако Waterius</h2>";
    h += field("period", "Период выхода на связь, мин", String(st.periodMin), "number");
    h += field("host", "Сервер", String(st.host));
    h += field("key", "Ключ", String(st.key));
    h += field("email", "Почта", String(st.email));

    h += "<h2>Wi-Fi</h2>";
    h += field("ssid", "Сеть", String(st.ssid));
    h += field("wpwd", "Пароль", String(st.pass), "password");

    h += "<button>Сохранить</button></form>";
    h += "<p class='s'>После сохранения Wi-Fi или порта RFC 2217 плата перезагрузится.</p>";
    h += "</body></html>";
    return h;
}

void handleRoot() { server.send(200, "text/html; charset=utf-8", page()); }

void handleRead() {
    app.readNow = true;
    server.sendHeader("Location", "/");
    server.send(303);
}

uint32_t argU32(const char* name, uint32_t def) {
    if (!server.hasArg(name)) return def;
    return (uint32_t)server.arg(name).toInt();
}

void argStr(const char* name, char* dst, size_t cap) {
    if (!server.hasArg(name)) return;
    snprintf(dst, cap, "%s", server.arg(name).c_str());
}

void handleSave() {
    Settings& st = app.sett;
    String oldSsid = st.ssid, oldPass = st.pass;
    uint16_t oldRfc = st.rfcPort;

    st.serial.baud = argU32("baud", st.serial.baud);
    st.serial.bits = (uint8_t)argU32("bits", st.serial.bits);
    st.serial.stop = (uint8_t)argU32("stop", st.serial.stop);
    if (server.hasArg("parity")) st.serial.parity = server.arg("parity")[0];
    st.rfcPort = (uint16_t)argU32("rfc", st.rfcPort);
    st.meterAddr = (uint8_t)argU32("addr", st.meterAddr);
    argStr("mpwd", st.meterPwd, sizeof(st.meterPwd));
    st.periodMin = (uint16_t)argU32("period", st.periodMin);
    if (st.periodMin < 1) st.periodMin = 1;
    argStr("host", st.host, sizeof(st.host));
    argStr("key", st.key, sizeof(st.key));
    argStr("email", st.email, sizeof(st.email));
    argStr("ssid", st.ssid, sizeof(st.ssid));
    argStr("wpwd", st.pass, sizeof(st.pass));

    settingsSave(st);
    applySerialCfg();

    bool reboot = oldSsid != st.ssid || oldPass != st.pass || oldRfc != st.rfcPort;
    server.sendHeader("Location", "/");
    server.send(303);
    if (reboot) {
        delay(300);
        ESP.restart();
    }
}

}  // namespace

void begin() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/read", HTTP_POST, handleRead);
    server.onNotFound(handleRoot);
    server.begin();
}

void loop() { server.handleClient(); }

}  // namespace web
