#include "power.h"

#include "app.h"
#include "settings.h"
#include "boards/board.h"
#include "ui/anim.h"
#include "ui/theme.h"

namespace power {
namespace {
constexpr uint32_t ALERT_MS = 2 * 60 * 1000;  // mudou de faixa (70/80/90%): brilho cheio por 2 min

uint32_t lastWakeMs = 0;
float level = 255, fullNow = 255;  // fullNow: o brilho cheio de agora (o do USB ou o da bateria)
int written = -1;

bool night() {
  const char* clk = app::frame["clk"] | "";  // horário de Brasília vindo do Mac
  if (strlen(clk) < 2) return false;
  const int h = atoi(clk);
  return h >= 22 || h < 7;  // modo noite fixo: 22h às 7h
}
}  // namespace

bool onBattery();
void trackDischarge();

bool idle() {
  const bool alert = anim::alertMs && millis() - anim::alertMs < ALERT_MS;
  return settings::idleMs() && millis() - lastWakeMs > settings::idleMs() && !alert;
}

void update() {
  trackDischarge();
  // na bateria, o brilho próprio dela (70% de fábrica): a AMOLED gasta mais quanto mais acesa
  const float full = fullNow = onBattery() ? settings::batteryBrightness() : settings::brightness();
  // parado escurece para 45% do máximo; à noite, para 8%
  const float target = !idle() ? full : night() ? max(6.f, full * 0.08f) : full * 0.45f;
  level += (target - level) * (target > level ? 0.25f : 0.04f);  // acende rápido, apaga devagar
  if (app::approveMs && millis() - app::approveMs < 20000) level = full * (0.35f + 0.65f * (1 - breath(1200)));  // pede aprovação
  const int v = lroundf(level);
  if (v != written) board::setBrightness(v), written = v;
}

// contra o cheio de agora: na bateria o brilho é menor e a tela não está escurecida (o toque não pode virar só "acender")
bool dimmed() { return level < fullNow * 0.8f; }

void wake() { lastWakeMs = millis(); }

void rest() { lastWakeMs = millis() - max<uint32_t>(settings::idleMs(), 1) - 1; }

uint32_t lastActiveMs() { return lastWakeMs; }

// O ritmo de descarga, medido desde que o cabo saiu: precisa cair 2 pontos para virar previsão. O último
// ritmo medido fica para a próxima vez que o cabo sair, então a previsão aparece logo.
uint32_t dropT0 = 0;
int dropP0 = -1;
float pctPerMs = 0;

void trackDischarge() {
  board::PowerState s;
  if (!board::PROFILE.battery || !board::powerState(s) || !s.battery || s.usb || s.pct < 0) return void(dropP0 = -1);
  if (dropP0 < 0 || s.pct > dropP0) dropT0 = millis(), dropP0 = s.pct;
  if (dropP0 - s.pct >= 2) pctPerMs = (dropP0 - s.pct) / float(millis() - dropT0);
}

int batteryLeftMin() {
  board::PowerState s;
  if (pctPerMs <= 0 || !board::powerState(s) || s.pct < 0) return -1;
  return s.pct / pctPerMs / 60000;
}

bool onBattery() {
  board::PowerState s;
  return board::PROFILE.battery && board::powerState(s) && s.battery && !s.usb;
}

}  // namespace power
