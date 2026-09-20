#include "storage.h"

#include <Preferences.h>

#include <string.h>

#include "../core/settings_io.h"
#include "../core/settings_v4.h"
#include "log.h"

namespace storage {
namespace {

const char* NS = "opto";
const char* KEY_CFG = "cfg";
// Блоб настроек прежних прошивок. Читается один раз, при переносе на `cfg`.
const char* KEY_SETTINGS = "settings";
const char* KEY_READING = "reading";
const char* KEY_OTA_ERROR = "ota_error";
const char* KEY_FAST = "fast";
const char* KEY_BOOTS = "boots";
const char* KEY_RESTART = "restart";

// Пара для быстрого коннекта — отдельно от записи настроек, см. storage.h
struct StoredFastConnect {
    uint8_t channel;
    uint8_t bssid[6];
};

// Записи настроек с запасом: предельные настройки занимают 971 байт.
const size_t CFG_CAP = 1536;

struct StoredReading {
    core::MeterData data;
    uint32_t readAt;
};

// Размер сверяется: запись от другой версии структуры не читается.
bool loadBlob(const char* key, void* dst, size_t size) {
    Preferences p;
    if (!p.begin(NS, true)) return false;
    bool ok = p.getBytesLength(key) == size && p.getBytes(key, dst, size) == size;
    p.end();
    return ok;
}

void saveBlob(const char* key, const void* src, size_t size) {
    Preferences p;
    if (!p.begin(NS, false)) return;
    p.putBytes(key, src, size);
    p.end();
}

void removeKey(const char* key) {
    Preferences p;
    if (!p.begin(NS, false)) return;
    p.remove(key);
    p.end();
}

// Канал и BSSID лежат отдельным ключом и выигрывают у записи настроек: роуминг
// переписывает только их, а в `cfg` пара могла остаться прошлая.
void applyFastConnect(core::Settings& s) {
    StoredFastConnect fast;
    if (!loadBlob(KEY_FAST, &fast, sizeof(fast))) return;
    s.channel = fast.channel;
    memcpy(s.bssid, fast.bssid, sizeof(s.bssid));
}

// Разовый перенос блоба прежних прошивок. Старый ключ удаляется только после
// подтверждённой записи нового: иначе сбой записи стёр бы настройки насовсем.
bool migrateFromV4(core::Settings& s) {
    size_t len = 0;
    {
        Preferences p;
        if (p.begin(NS, true)) {
            len = p.getBytesLength(KEY_SETTINGS);
            p.end();
        }
    }
    if (!len) return false;  // записи прежних прошивок нет — плата чистая

    core::SettingsV4 v4;
    if (len != sizeof(v4) || !loadBlob(KEY_SETTINGS, &v4, sizeof(v4)) || v4.version != 4) {
        Log.warn("Настройки: запись `settings` (%u байт) не читается, беру умолчания\n",
                 (unsigned)len);
        return false;
    }
    core::fromV4(v4, s);
    if (!saveSettings(s)) {
        Log.error("Настройки: формат 4 прочитан, но не сохранился — старая запись цела\n");
        return true;  // настройки всё равно в руках, перенос повторится на следующей загрузке
    }
    removeKey(KEY_SETTINGS);
    Log.info("Настройки перенесены из формата 4\n");
    return true;
}

}  // namespace

void loadSettings(core::Settings& s) {
    s = core::Settings();

    String cfg;
    {
        Preferences p;
        if (p.begin(NS, true)) {
            cfg = p.getString(KEY_CFG, String());
            p.end();
        }
    }

    if (cfg.length()) {
        switch (core::settingsFromJson(cfg.c_str(), cfg.length(), s)) {
            case core::LoadResult::Ok:
                Log.info("Настройки загружены\n");
                break;
            case core::LoadResult::Migrated:
                // Цепочка апгрейдов что-то сделала — закрепляем результат сразу,
                // иначе она будет гоняться на каждой загрузке
                Log.info("Настройки подняты до версии %u\n", (unsigned)core::SETTINGS_VERSION);
                if (!saveSettings(s)) Log.error("Настройки: поднятую версию не удалось сохранить\n");
                break;
            case core::LoadResult::FromFuture:
                // Откат прошивки: перезаписывать запись нельзя, в ней поля,
                // которых эта прошивка не знает
                Log.warn("Настройки новее прошивки, прочитано только понятное\n");
                break;
            case core::LoadResult::Defaults:
                Log.warn("Настройки не разбираются, беру умолчания\n");
                break;
        }
    } else if (!migrateFromV4(s)) {
        Log.info("Настроек в памяти нет, беру умолчания\n");
    }

    applyFastConnect(s);
}

bool saveSettings(const core::Settings& s) {
    // Буфера хватает на самые длинные настройки, какие можно ввести на
    // странице (замер — test/test_settings)
    char json[CFG_CAP];
    size_t n = core::settingsToJson(s, json, sizeof(json));
    if (!n) return false;  // не поместилось: обрезанный JSON не разберётся, старую запись не трогаем

    Preferences p;
    if (!p.begin(NS, false)) return false;
    size_t written = p.putString(KEY_CFG, json);
    p.end();
    return written == n;
}

void saveFastConnect(const core::Settings& s) {
    StoredFastConnect fast;
    fast.channel = s.channel;
    memcpy(fast.bssid, s.bssid, sizeof(fast.bssid));
    saveBlob(KEY_FAST, &fast, sizeof(fast));
}

uint8_t loadBootCount() {
    Preferences p;
    if (!p.begin(NS, true)) return 0;
    uint8_t v = p.getUChar(KEY_BOOTS, 0);
    p.end();
    return v;
}

void saveBootCount(uint8_t count) {
    Preferences p;
    if (!p.begin(NS, false)) return;
    p.putUChar(KEY_BOOTS, count);
    p.end();
}

core::RestartReason loadRestartReason() {
    Preferences p;
    if (!p.begin(NS, true)) return core::RestartReason::Unknown;
    uint8_t v = p.getUChar(KEY_RESTART, 0);
    p.end();
    return (core::RestartReason)v;
}

void saveRestartReason(core::RestartReason reason) {
    Preferences p;
    if (!p.begin(NS, false)) return;
    p.putUChar(KEY_RESTART, (uint8_t)reason);
    p.end();
}

bool loadLastReading(core::MeterData& m, uint32_t& readAt) {
    StoredReading r;
    if (!loadBlob(KEY_READING, &r, sizeof(r))) return false;
    m = r.data;
    readAt = r.readAt;
    return true;
}

void saveLastReading(const core::MeterData& m, uint32_t readAt) {
    StoredReading r;
    r.data = m;
    r.readAt = readAt;
    saveBlob(KEY_READING, &r, sizeof(r));
}

uint8_t loadOtaError() {
    Preferences p;
    if (!p.begin(NS, true)) return 0;
    uint8_t v = p.getUChar(KEY_OTA_ERROR, 0);
    p.end();
    return v;
}

void saveOtaError(uint8_t code) {
    Preferences p;
    if (!p.begin(NS, false)) return;
    p.putUChar(KEY_OTA_ERROR, code);
    p.end();
}

void resetAll() {
    Preferences p;
    if (!p.begin(NS, false)) return;
    p.clear();
    p.end();
}

}  // namespace storage
