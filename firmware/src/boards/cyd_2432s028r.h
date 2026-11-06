#pragma once
// ESP32-2432S028R, a "Cheap Yellow Display" (CYD): ESP32-D0WD-V3 sem PSRAM, 4 MB de flash, TFT 2,8" 240x320
// ST7789 por SPI, touch resistivo XPT2046 num SPI próprio, LED RGB, LDR, SD e saída de alto-falante.
// USB por CH340: a serial é a UART0, e a velocidade precisa ser a mesma do daemon.
// https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display
#include <Arduino.h>
#include <LovyanGFX.hpp>

#include <algorithm>

#include "../settings.h"
#include "profile.h"

namespace board {

constexpr const char* NAME = "cyd-2432s028r";
constexpr uint8_t DEFAULT_ROTATION = 0;  // retrato
constexpr uint8_t ROTATIONS = 4;  // o cabo USB pode sair por qualquer lado: em pé e deitada, nos dois sentidos
constexpr uint8_t ROT_FOR[4] = {0, 0, 0, 0};
constexpr Profile PROFILE{.touch = true, .calibrate = true, .imu = false, .led = true, .button = true};
constexpr uint32_t SERIAL_BAUD = 460800;  // o CH340 desta placa perde bytes acima disso

class Lcd : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;

 public:
  Lcd() {
    auto b = bus_.config();
    b.spi_host = HSPI_HOST;
    b.freq_write = 55000000;
    b.freq_read = 16000000;
    b.pin_sclk = 14;
    b.pin_mosi = 13;
    b.pin_miso = 12;
    b.pin_dc = 2;
    bus_.config(b);
    panel_.setBus(&bus_);

    auto p = panel_.config();
    p.pin_cs = 15;
    p.pin_rst = -1;
    p.panel_width = 240;
    p.panel_height = 320;
    p.invert = false;  // o painel desta placa já vem com as cores certas; com inversão, o tema escuro sai claro
    p.readable = false;
    panel_.config(p);

    auto l = light_.config();
    l.pin_bl = 21;
    l.freq = 12000;
    l.pwm_channel = 7;
    light_.config(l);
    panel_.setLight(&light_);
    setPanel(&panel_);
  }
};

inline Lcd lcd;

inline void displayBegin() {
  lcd.init();
  lcd.setBrightness(255);
}
inline void setBrightness(uint8_t v) { lcd.setBrightness(v); }
inline uint8_t rotation = 0;
inline void displaySetRotation(uint8_t rot) { lcd.setRotation(rot), rotation = rot & 3; }
inline int displayWidth() { return lcd.width(); }
inline int displayHeight() { return lcd.height(); }
inline void displayPush(const uint16_t* px, int y, int w, int h) {
  lcd.pushImage(0, y, w, h, reinterpret_cast<const lgfx::swap565_t*>(px));
}

constexpr int PIN_BUTTON = 0;  // BOOT
constexpr int PIN_LED[3] = {4, 16, 17};  // R, G, B; acendem em nível baixo

// XPT2046 lido bit a bit: o SPI dele (CLK 25, MOSI 32, MISO 39, CS 33) não é o da tela.
constexpr int T_CLK = 25, T_MOSI = 32, T_MISO = 39, T_CS = 33, T_IRQ = 36;
// Sem calibração gravada vale esta, de uma placa de teste. A calibração da primeira vez troca pela desta placa.
constexpr int RAW_X0 = 3865, RAW_X1 = 287, RAW_Y0 = 161, RAW_Y1 = 3773;
// Pressão mínima (Z1 + 4095 - Z2). Encostar de leve dá coordenada com ruído: abaixo disso, não é toque.
constexpr int PRESS_MIN = 400;

inline uint16_t xpt(uint8_t cmd) {
  auto xfer = [](uint8_t out) {
    uint8_t in = 0;
    for (int i = 7; i >= 0; i--) {
      digitalWrite(T_MOSI, out >> i & 1);
      digitalWrite(T_CLK, 1);
      in |= digitalRead(T_MISO) << i;
      digitalWrite(T_CLK, 0);
    }
    return in;
  };
  digitalWrite(T_CS, 0);
  xfer(cmd);
  uint16_t v = xfer(0) << 8;
  v |= xfer(0);
  digitalWrite(T_CS, 1);
  return v >> 3;
}

inline void begin() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  for (int i = 0; i < 3; i++) ledcSetup(1 + i, 5000, 8), ledcAttachPin(PIN_LED[i], 1 + i), ledcWrite(1 + i, 255);
  pinMode(T_CLK, OUTPUT), pinMode(T_MOSI, OUTPUT), pinMode(T_CS, OUTPUT), pinMode(T_MISO, INPUT), pinMode(T_IRQ, INPUT);
  digitalWrite(T_CS, 1), digitalWrite(T_CLK, 0);
}

inline bool gravity(int16_t&, int16_t&) { return false; }

inline void setLed(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(1, 255 - r), ledcWrite(2, 255 - g), ledcWrite(3, 255 - b);
}

inline bool buttonPressed() { return digitalRead(PIN_BUTTON) == LOW; }

// Leitura crua no eixo do painel, só com pressão firme. A mediana de 5 amostras tira os saltos do resistivo.
inline bool touchRaw(int16_t& rx, int16_t& ry) {
  if (digitalRead(T_IRQ)) return false;
  if (xpt(0xB0) + 4095 - xpt(0xC0) < PRESS_MIN) return false;
  auto median = [](uint8_t cmd) {
    uint16_t v[5];
    for (auto& x : v) x = xpt(cmd);
    std::sort(v, v + 5);
    return v[2];
  };
  rx = median(0xD0), ry = median(0x90);
  return !digitalRead(T_IRQ);  // soltou durante a leitura: a amostra não vale
}

// Um ponto na tela, em pixels da orientação atual.
inline bool touch(int16_t& x, int16_t& y) {
  static uint32_t lastMs = 0;
  static int16_t lx = 0, ly = 0;
  int16_t rx, ry;
  if (touchRaw(rx, ry)) {
    const auto& c = settings::cal;
    const int a = c.ok && c.swap ? ry : rx, b = c.ok && c.swap ? rx : ry;
    const int px = constrain(map(a, c.ok ? c.x0 : RAW_X0, c.ok ? c.x1 : RAW_X1, 0, 240), 0, 239);
    const int py = constrain(map(b, c.ok ? c.y0 : RAW_Y0, c.ok ? c.y1 : RAW_Y1, 0, 320), 0, 319);
    // o retrato girado: 1 deitada (90° horário), 2 em pé de cabeça para baixo, 3 deitada no outro sentido
    if (rotation == 1) lx = py, ly = 239 - px;
    else if (rotation == 2) lx = 239 - px, ly = 319 - py;
    else if (rotation == 3) lx = 319 - py, ly = px;
    else lx = px, ly = py;
    lastMs = millis();
  } else if (millis() - lastMs > 50) {
    return false;  // a caneta perde contato por instantes no meio do arrasto: até 50 ms ainda é o mesmo toque
  }
  x = lx, y = ly;
  return true;
}

}  // namespace board
