// Порт: хранение настроек в NVS.
#include <Preferences.h>

#include "../settings.h"

namespace {
const char* NS = "opto";
const char* KEY = "settings";
const uint32_t MAGIC = 0x4F505431;  // "OPT1" — версия формата
}  // namespace

void settingsLoad(Settings& s) {
    Preferences p;
    if (!p.begin(NS, true)) return;
    uint32_t magic = p.getULong("magic", 0);
    if (magic == MAGIC) p.getBytes(KEY, &s, sizeof(s));
    p.end();
}

void settingsSave(const Settings& s) {
    Preferences p;
    if (!p.begin(NS, false)) return;
    p.putBytes(KEY, &s, sizeof(s));
    p.putULong("magic", MAGIC);
    p.end();
}
