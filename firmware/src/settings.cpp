#include "settings.h"

#include <Preferences.h>

namespace settings {

Values v;

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
  prefs.end();
  // valores fora da faixa (NVS corrompida) voltam ao padrão campo a campo
  const Values d;
  if (v.bright > 3) v.bright = d.bright;
  if (v.idle > 3) v.idle = d.idle;
  if (v.theme > 2) v.theme = d.theme;
  if (v.flip > 1) v.flip = d.flip;
  if (v.hideCost > 1) v.hideCost = d.hideCost;
}

void save() {
  if (!prefs.begin(NS, false)) return;
  prefs.putUChar("ver", VERSION);
  prefs.putBytes("v", &v, sizeof v);
  prefs.end();
}

uint8_t brightness() { return 64 * (min<int>(v.bright, 3) + 1) - 1; }

uint32_t idleMs() {
  static const uint32_t MIN[] = {5, 10, 30, 0};
  return MIN[min<int>(v.idle, 3)] * 60000;
}



}  // namespace settings
