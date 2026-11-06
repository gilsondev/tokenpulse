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
float level = 255;
int written = -1;

bool night() {
  const char* clk = app::frame["clk"] | "";  // horário de Brasília vindo do Mac
  if (strlen(clk) < 2) return false;
  const int h = atoi(clk);
  return h >= 22 || h < 7;  // modo noite fixo: 22h às 7h
}
}  // namespace

void update() {
  const float full = settings::brightness();
  const bool idle = settings::idleMs() && millis() - lastWakeMs > settings::idleMs();
  const bool alert = anim::alertMs && millis() - anim::alertMs < ALERT_MS;
  // parado escurece para 45% do máximo; à noite, para 8%
  const float target = !idle || alert ? full : night() ? max(6.f, full * 0.08f) : full * 0.45f;
  level += (target - level) * (target > level ? 0.25f : 0.04f);  // acende rápido, apaga devagar
  if (app::approveMs && millis() - app::approveMs < 20000) level = full * (0.35f + 0.65f * (1 - breath(1200)));  // pede aprovação
  const int v = lroundf(level);
  if (v != written) board::setBrightness(v), written = v;
}

bool dimmed() { return level < settings::brightness() * 0.8f; }

void wake() { lastWakeMs = millis(); }

}  // namespace power
