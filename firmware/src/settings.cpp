#include "settings.h"

#include <Preferences.h>

namespace settings {

Values v;
TouchCal cal;
Web web;

namespace {
Preferences prefs;
constexpr const char* NS = "ajustes";
}  // namespace

// Mudou a ordem ou o significado dos campos de Values? Suba a versão: o gravado antigo é ignorado.
constexpr uint8_t VERSION = 1;

void begin() {
  if (!prefs.begin(NS, true)) return;  // primeira vez: ainda não existe, ficam os padrões
  Values read;
  if (prefs.getUChar("ver", 0) == VERSION && prefs.getBytes("v", &read, sizeof read) == sizeof read) v = read;
  TouchCal c;
  if (prefs.getBytes("cal", &c, sizeof c) == sizeof c && c.ok) cal = c;
  Web w;
  if (prefs.getBytes("web", &w, sizeof w) == sizeof w) {
    w.host[sizeof w.host - 1] = 0;
    const Web d;
    web = w;
    if (web.interval > 120) web.interval = d.interval;
    if (web.pages > 7) web.pages = d.pages;
    if (web.alerts > 1) web.alerts = d.alerts;
    if (web.wifi > 1) web.wifi = d.wifi;
    if (web.host[0] && !validHost(web.host)) web.host[0] = 0;
  }
  prefs.end();
  // valores fora da faixa (NVS corrompida) voltam ao padrão campo a campo
  const Values d;
  if (v.bright > 3) v.bright = d.bright;
  if (v.idle > 3) v.idle = d.idle;
  if (v.theme > 2) v.theme = d.theme;
  if (v.orient > 1) v.orient = d.orient;
  if (v.hideCost > 1) v.hideCost = d.hideCost;
}

void save() {
  if (!prefs.begin(NS, false)) return;
  prefs.putUChar("ver", VERSION);
  prefs.putBytes("v", &v, sizeof v);
  prefs.end();
}

void saveCal() {
  if (!prefs.begin(NS, false)) return;
  prefs.putBytes("cal", &cal, sizeof cal);
  prefs.end();
}

void saveWeb() {
  if (!prefs.begin(NS, false)) return;
  prefs.putBytes("web", &web, sizeof web);
  prefs.end();
}

bool validHost(const String& h) {
  if (h.isEmpty() || h.length() > 31 || h[0] == '-' || h[h.length() - 1] == '-') return false;
  for (char ch : h)
    if (!isdigit(ch) && !(ch >= 'a' && ch <= 'z') && ch != '-') return false;
  return true;
}

uint8_t brightness() { return 64 * (min<int>(v.bright, 3) + 1) - 1; }

uint32_t idleMs() {
  static const uint32_t MIN[] = {5, 10, 30, 0};
  return MIN[min<int>(v.idle, 3)] * 60000;
}



}  // namespace settings
