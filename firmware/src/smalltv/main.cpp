// tokenpulse mínimo para o GeekMagic SmallTV-Ultra (ESP8266, 240x240, USB só de energia).
// O WiFi e o /update sobem antes da tela: sem serial, eles são o único caminho para corrigir a placa.
#include <Arduino.h>
#include <user_interface.h>

#include "app.h"
#include "config.h"
#include "net.h"
#include "screen.h"

namespace {

// Contador de travamentos na memória RTC, que sobrevive ao reset. Três reinícios seguidos por exceção
// ou watchdog antes de 1 min de uptime: a próxima volta sobe só WiFi e /update, sem a tela.
struct Guard {
  uint32_t magic, crashes;
};
constexpr uint32_t MAGIC = 0x7470756c;
Guard guard;

void checkGuard() {
  ESP.rtcUserMemoryRead(0, reinterpret_cast<uint32_t*>(&guard), sizeof guard);
  if (guard.magic != MAGIC) guard = {MAGIC, 0};
  const uint32_t r = ESP.getResetInfoPtr()->reason;
  const bool crashed = r == REASON_WDT_RST || r == REASON_EXCEPTION_RST || r == REASON_SOFT_WDT_RST;
  guard.crashes = crashed ? guard.crashes + 1 : 0;
  app::safe = guard.crashes >= 3;
  ESP.rtcUserMemoryWrite(0, reinterpret_cast<uint32_t*>(&guard), sizeof guard);
}

void clearGuard() {
  static bool done = false;
  if (done || millis() < 60000) return;
  done = true;
  guard.crashes = 0;
  ESP.rtcUserMemoryWrite(0, reinterpret_cast<uint32_t*>(&guard), sizeof guard);
}

}  // namespace

void setup() {
  checkGuard();
  config::begin();
  net::begin();
  if (!app::safe) screen::begin();
}

void loop() {
  net::loop();
  clearGuard();
  if (!app::safe) screen::update(net::takeFrame());
}
