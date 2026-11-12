#include "anim.h"

#include "../app.h"
#include "theme.h"

namespace anim {

float p5 = 0, pw = 0, grow = 0;
uint32_t tokensMs = 0, alertMs = 0, pwMs = 0;
uint16_t alertFrom = color::GREEN;

namespace {
String lastTot;
int lastLevel = 0, lastPw = -1;
float v5 = 0, vw = 0;
uint32_t lastStepMs = 0, enterMs = 0;

// Mola amortecida (Material 3 Expressive, DefaultSpatial: k=380, ζ=0,8): chega com um leve overshoot.
void spring(float& x, float& v, float target, float dt) {
  constexpr float K = 380, C = 2 * 0.8f * 19.4936f;  // 2ζ√k
  for (int i = 0; i < 2; i++) {
    v += (K * (target - x) - C * v) * dt / 2;
    x += v * dt / 2;
  }
}

// Resposta ao degrau de uma mola: 0 em t=0, passa um pouco de 1 e assenta em 1.
float stepResponse(float t, float k, float zeta) {
  if (t <= 0) return 0;
  const float w = sqrtf(k), wd = w * sqrtf(1 - zeta * zeta);
  return 1 - expf(-zeta * w * t) * (cosf(wd * t) + zeta * w / wd * sinf(wd * t));
}
}  // namespace

void onFrame() {
  String tot = app::frame["tot"] | "";
  if (lastTot.length() && tot != lastTot) tokensMs = millis();
  lastTot = tot;
  const int p = app::frame["p5"] | -1;
  int lv = level(p);
  if (lv > lastLevel) alertMs = millis(), alertFrom = levelColor(lastLevel == 0 ? 0 : lastLevel == 1 ? 70 : 80);
  lastLevel = lv;
  const int w = app::frame["pw"] | -1;
  if (lastPw >= 0 && w != lastPw) pwMs = millis();
  lastPw = w;
}

void step(JsonVariantConst src) {
  const uint32_t now = millis();
  const float dt = constrain((now - lastStepMs) / 1000.f, 0.f, 0.05f);
  lastStepMs = now;
  spring(p5, v5, max((int)(src["p5"] | -1), 0), dt);
  spring(pw, vw, max((int)(src["pw"] | -1), 0), dt);
  grow += (1 - grow) * 0.12f;
}

void reset() {
  p5 = pw = grow = v5 = vw = 0;
  enterMs = pwMs = millis();
}

float entrance(int i) { return stepResponse((millis() - enterMs) / 1000.f - 0.06f * i, 800, 0.6f); }

float halo() {
  float a = 0.25f + 0.15f * breath(4000);
  const float t = since(alertMs, 800);
  if (t < 1) a = max(a, 0.7f * (t < 0.375f ? t / 0.375f : 1 - (t - 0.375f) / 0.625f));
  return a;
}

}  // namespace anim
