// Monitor de tokens do Claude: recebe os dados do daemon do Mac pela USB e desenha as telas.
// Hardware específico fica em boards/; telas em ui/; protocolo serial e OTA em hostlink.cpp.
#include <Arduino.h>

#include "boards/board.h"
#include "hostlink.h"
#include "power.h"
#include "settings.h"
#include "webui.h"
#include "ui/anim.h"
#include "ui/gfx.h"
#include "ui/screens.h"

namespace {

constexpr int16_t TILT_MIN = 9000;  // ~0,55 g no plano da tela; abaixo disso a placa está deitada

// Gira a tela pela gravidade. Lê a cada 150 ms e só gira depois de 3 leituras iguais, para não piscar.
void checkOrientation() {
  static uint32_t lastMs = 0;
  static int candidate = -1, count = 0;
  if (millis() - lastMs < 150) return;
  lastMs = millis();
  int16_t x, y;
  if (!board::gravity(x, y)) return;
  int side = -1;
  if (abs(x) > TILT_MIN && abs(x) > abs(y) * 3 / 2) side = x > 0 ? 0 : 1;
  if (abs(y) > TILT_MIN && abs(y) > abs(x) * 3 / 2) side = y > 0 ? 2 : 3;
  if (side < 0) return;  // deitada ou na diagonal: mantém a rotação atual
  count = side == candidate ? count + 1 : 1;
  candidate = side;
  if (count == 3 && board::ROT_FOR[side] != gfx::rotation()) {
    Serial.printf("gira: lado=%d rot=%d acc=%d,%d\n", side, board::ROT_FOR[side], x, y);
    gfx::setRotation(board::ROT_FOR[side]);
    anim::reset();
  }
}

// Clique avança a tela; clique longo (> 0,7 s) volta para a principal.
void checkButton() {
  static bool down = false;
  static uint32_t downMs = 0;
  bool pressed = board::buttonPressed();
  if (pressed && !down) down = true, downMs = millis(), power::wake();
  if (!pressed && down) {
    down = false;
    uint32_t held = millis() - downMs;
    if (held > 700) screens::home();
    else if (held > 30) screens::next();
  }
}

// Gestos: arrastar para os lados troca de tela, para cima/baixo rola, da borda de baixo para cima volta
// para a principal; toque curto abre o que foi tocado; segurar também volta para a principal.
void checkTouch() {
  static bool down = false, held = false;
  static int16_t x0, y0, x, y;
  static uint32_t downMs;
  int16_t tx, ty;
  static bool swallow = false;  // toque que só acendeu a tela
  if (board::touch(tx, ty)) {
    if (!down) {
      down = true, held = false, x0 = tx, y0 = ty, downMs = millis();
      swallow = power::dimmed();
      power::wake();
    }
    if (swallow) return;
    x = tx, y = ty;
    if (!held && millis() - downMs > 650 && abs(x - x0) < 24 && abs(y - y0) < 24) held = true, screens::home();
    return;
  }
  if (!down) return;
  down = false;
  if (swallow) return;
  const int dx = x - x0, dy = y - y0;
  Serial.printf("toque: %d,%d -> %d,%d em %lu ms\n", x0, y0, x, y, millis() - downMs);
  if (held) return;
  // o swipe mínimo acompanha o lado menor: 40 px em 320, 30 px em 240 (com caneta o gesto sai mais curto)
  const int swipe = min(gfx::width(), gfx::height()) / 8;
  // arrastar para cima a partir da borda de baixo: volta para a principal de qualquer lugar, como no celular
  if (y0 > gfx::height() - gfx::height() * 3 / 40 && dy < -gfx::height() * 5 / 48 && abs(dy) > abs(dx)) screens::homeUp();
  else if (abs(dx) > swipe && abs(dx) > 2 * abs(dy)) dx < 0 ? screens::next() : screens::prev();
  else if (abs(dy) > swipe && abs(dy) > 2 * abs(dx)) screens::scroll(dy < 0 ? 1 : -1);
  else if (abs(dx) < 24 && abs(dy) < 24) screens::tap(x0, y0);
}

}  // namespace

void setup() {
  hostlink::begin();
  settings::begin();
  gfx::begin();
  board::begin();
  if (settings::v.orient) gfx::setRotation(1);
  screens::bootSplash();
  // touch resistivo sem calibração gravada: calibra antes de tudo, senão os toques caem longe da caneta
  if (board::PROFILE.calibrate && !settings::cal.ok) screens::calibrateTouch();
  webui::begin();
}

void loop() {
  static uint32_t lastRenderMs = 0;
  if (board::PROFILE.imu) checkOrientation();
  if (board::PROFILE.button) checkButton();
  if (board::PROFILE.touch) checkTouch();
  hostlink::poll();
  webui::loop();
  if (millis() - lastRenderMs >= 33) {  // ~30 fps; o envio do quadro anterior corre no outro núcleo
    lastRenderMs = millis();
    screens::render();
  }
}
