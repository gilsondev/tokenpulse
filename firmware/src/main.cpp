// Monitor de tokens do Claude: recebe os dados do daemon do Mac pela USB e desenha as telas.
// Hardware específico fica em boards/; telas em ui/; protocolo serial e OTA em hostlink.cpp.
#include <Arduino.h>

#include "app.h"
#include "ble.h"
#include "sd.h"
#include "boards/board.h"
#include "hostlink.h"
#include "power.h"
#include "settings.h"
#include "webui.h"
#include "ui/anim.h"
#include "ui/gfx.h"
#include "ui/screens.h"

#include <esp_ota_ops.h>

// Firmware novo só vira "bom" depois de rodar (marcado no loop). Se travar ou reiniciar antes, o bootloader volta
// para a versão anterior sozinho: um release com defeito de boot não fica preso na placa.
extern "C" bool verifyRollbackLater() { return true; }

#ifdef BOARD_HAS_PSRAM
// O VLW de 120 px do descanso estoura os 8 KB padrão da loopTask; com PSRAM a RAM interna sobra.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif

namespace {

constexpr int16_t TILT_MIN = 9000;  // ~0,55 g no plano da tela; abaixo disso a placa está deitada

// Lado para onde a gravidade aponta (0 +X, 1 -X, 2 +Y, 3 -Y), ou -1 deitada ou na diagonal.
int gravitySide() {
  int16_t x, y;
  if (!board::gravity(x, y)) return -1;
  if (abs(x) > TILT_MIN && abs(x) > abs(y) * 3 / 2) return x > 0 ? 0 : 1;
  if (abs(y) > TILT_MIN && abs(y) > abs(x) * 3 / 2) return y > 0 ? 2 : 3;
  return -1;
}

// Gira a tela pela gravidade. Lê a cada 150 ms e só gira depois de 3 leituras iguais, para não piscar.
void checkOrientation() {
  static uint32_t lastMs = 0;
  static int candidate = -1, count = 0;
  if (millis() - lastMs < 150) return;
  lastMs = millis();
  const int side = gravitySide();
  if (side < 0) return;  // deitada ou na diagonal: mantém a rotação atual
  count = side == candidate ? count + 1 : 1;
  candidate = side;
  if (count == 3 && board::ROT_FOR[side] != gfx::rotation()) {
    Serial.printf("gira: lado=%d rot=%d\n", side, board::ROT_FOR[side]);
    gfx::setRotation(board::ROT_FOR[side]);
    anim::reset();
  }
}

// Clique avança a tela; clique longo (> 0,7 s) volta para a principal.
void checkButton() {
  static bool down = false;
  static uint32_t downMs = 0;
  static bool swallow = false;  // aperto que só acendeu a tela
  bool pressed = board::buttonPressed();
  if (pressed && !down) down = true, downMs = millis(), swallow = power::dimmed(), power::wake();
  if (!pressed && down) {
    down = false;
    if (swallow) return;
    uint32_t held = millis() - downMs;
    if (held > 700) screens::home();
    else if (held > 30) screens::next();
  }
}

// PWR: um toque entra no descanso ou sai dele; segurar desliga.
// BOOT: um toque silencia o som do alerta por 1 h; outro religa e toca a prévia do som.
void checkMute() {
  static bool down = false;
  static uint32_t changedMs = 0;
  const bool pressed = board::mutePressed();
  if (pressed != down && millis() - changedMs < 50) return;  // o contato quica ao apertar: ignora por 50 ms
  if (pressed != down) changedMs = millis();
  if (pressed && !down) {
    power::wake();
    if (app::muted()) {
      app::muteUntil = 0;
      board::play(settings::web.sound, settings::volumePct());
    } else {
      app::muteUntil = (millis() + 3600000UL) | 1;
    }
  }
  down = pressed;
}

constexpr uint32_t SHUT_MS = 2000;  // PWR segurado: a tela avisa a partir de 0,5 s e desliga aos 2 s

void checkRest() {
  static bool down = false;
  static uint32_t downMs = 0;
  const bool pressed = board::restPressed();
  if (pressed && !down) downMs = millis();
  if (pressed) {
    const uint32_t held = millis() - downMs;
    screens::shuttingDown(held);
    if (held > SHUT_MS + 400) board::powerOff();  // o "até logo" fica 0,4 s na tela
  }
  if (!pressed && down) {
    if (millis() - downMs < 500) power::idle() || power::dimmed() ? power::wake() : power::rest();
    screens::shuttingDown(0);  // soltou antes dos 4 s: cancela
  }
  down = pressed;
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
  // arrastar para baixo a partir da borda de cima: abre os ajustes, como a cortina do celular
  else if (y0 < gfx::height() * 3 / 40 && dy > gfx::height() * 5 / 48 && abs(dy) > abs(dx)) screens::pullDown();
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
  sd::begin();
  uint32_t t;
  if (board::rtcRead(t)) app::syncClock(t);  // a hora do RTC até o Mac mandar a dele
  if (board::PROFILE.imu) {  // já na posição em que a placa está, desde a abertura
    int side = -1;
    for (uint32_t t0 = millis(); side < 0 && millis() - t0 < 300; delay(20)) side = gravitySide();
    if (side >= 0) gfx::setRotation(board::ROT_FOR[side]);
  } else {
    gfx::setRotation(settings::v.orient);  // a posição gravada, inclusive a 0 (o padrão da placa pode ser outro)
  }
  screens::bootSplash();
  // touch resistivo sem calibração gravada: calibra antes de tudo, senão os toques caem longe da caneta
  if (board::PROFILE.calibrate && !settings::cal.ok) screens::calibrateTouch();
  webui::begin();
#ifdef TOKENPULSE_BLE
  // o nome que o Mac lista no Bluetooth: o produto, a placa e um final do chip, que separa duas placas iguais
  static const String bleName = String("TokenPulse ") + board::BLE_LABEL + " " + String((uint32_t)(ESP.getEfuseMac() >> 24) & 0xFFFFFF, HEX);
  ble::begin(bleName.c_str());
#endif
  enableLoopWDT();  // loop parado por 5 s reinicia a placa (e, com firmware recém-gravado, dispara o rollback)
}

void loop() {
  static uint32_t lastRenderMs = 0;
  static bool valid = false;
  if (!valid && millis() > 20000) valid = true, esp_ota_mark_app_valid_cancel_rollback();  // 20 s rodando e desenhando
  if (board::PROFILE.imu) checkOrientation();
  if (board::PROFILE.button) checkButton();
  checkRest();
  checkMute();
  if (board::PROFILE.touch) checkTouch();
  hostlink::poll();
  webui::loop();
  // na bateria: WiFi parado (só serve para a página; volta no USB ou ao abrir a Página web), 5 quadros/s quando
  // ninguém toca há 3 s e uma pausa no loop
  static int battery = -1;
  const bool bat = power::onBattery();
  if (bat != battery) {
    battery = bat;
    bat ? webui::pause() : webui::resume();
  }
  const uint32_t every = bat && millis() - power::lastActiveMs() > 3000 ? 200 : 33;
  if (millis() - lastRenderMs >= every) {  // o envio do quadro anterior corre no outro núcleo
    lastRenderMs = millis();
    screens::render();
  }
  if (bat) delay(5);  // a CPU dorme entre as voltas em vez de girar o loop vazio
}
