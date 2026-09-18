#include "storage.h"

#include <Preferences.h>

namespace storage {
namespace {

const char* NS = "opto";
const char* KEY_SETTINGS = "settings";
const char* KEY_READING = "reading";
const char* KEY_OTA_ERROR = "ota_error";

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

}  // namespace

void loadSettings(core::Settings& s) {
    core::Settings stored;
    if (loadBlob(KEY_SETTINGS, &stored, sizeof(stored)) && stored.version == core::SETTINGS_VERSION) {
        s = stored;
    } else {
        s = core::Settings();
    }
}

void saveSettings(const core::Settings& s) { saveBlob(KEY_SETTINGS, &s, sizeof(s)); }

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
