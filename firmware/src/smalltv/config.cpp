#include "config.h"

#include <EEPROM.h>

namespace config {

Values v;
uint32_t version = 0;

namespace {
constexpr uint32_t MAGIC = 0x74700001;  // mudou o layout de Values? suba o número: o gravado antigo é ignorado
struct Stored {
  uint32_t magic;
  Values v;
};
}  // namespace

bool validHost(const String& h) {
  if (h.isEmpty() || h.length() > 31 || h[0] == '-' || h[h.length() - 1] == '-') return false;
  for (char c : h)
    if (!isdigit(c) && !(c >= 'a' && c <= 'z') && c != '-') return false;
  return true;
}

void begin() {
  Stored s;
  EEPROM.begin(sizeof s);
  EEPROM.get(0, s);
  if (s.magic != MAGIC) return;  // primeira vez, ou o setor tem o que o firmware de fábrica deixou
  s.v.host[sizeof s.v.host - 1] = 0;
  const Values d;
  v = s.v;
  if (v.bright < 10 || v.bright > 100) v.bright = d.bright;
  if (v.night > 1) v.night = d.night;
  if (v.theme > 2) v.theme = d.theme;
  if (v.rotation > 3) v.rotation = d.rotation;
  if (v.interval > 120) v.interval = d.interval;
  if (v.pages > 63) v.pages = d.pages;
  if (v.host[0] && !validHost(v.host)) v.host[0] = 0;
  if (v.hideCost > 1) v.hideCost = d.hideCost;
  if (v.alerts > 3) v.alerts = d.alerts;  // o byte além do layout antigo vem com o que havia na flash
}

void save() {
  EEPROM.put(0, Stored{MAGIC, v});
  EEPROM.commit();
  version++;
}

}  // namespace config
