#pragma once
// Waveshare ESP32-S3-LCD-1.47B: ESP32-S3R8, ST7789T 172x320 por SPI, QMI8658, LED RGB e botão BOOT.
// https://www.waveshare.com/wiki/ESP32-S3-LCD-1.47B
#include <Arduino.h>
#include <LovyanGFX.hpp>

#include "../drivers/qmi8658.h"
#include "profile.h"

namespace board {

constexpr const char* NAME = "waveshare-s3-lcd-147b";
constexpr uint8_t DEFAULT_ROTATION = 1;  // paisagem, USB à direita
constexpr uint8_t ROTATIONS = 2;  // gira sozinha pelo IMU; o ajuste não vale nela
// Calibrado segurando a placa nas 4 posições. Se uma ficar de cabeça para baixo, troque o valor dela.
constexpr uint8_t ROT_FOR[4] = {3, 1, 2, 0};
constexpr Profile PROFILE{.touch = false, .calibrate = false, .imu = true, .led = true, .button = true};
constexpr uint32_t SERIAL_BAUD = 115200;

class Lcd : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;

 public:
  Lcd() {
    auto b = bus_.config();
    b.spi_host = SPI2_HOST;
    b.freq_write = 40000000;
    b.pin_sclk = 40;
    b.pin_mosi = 45;
    b.pin_miso = -1;
    b.pin_dc = 41;
    bus_.config(b);
    panel_.setBus(&bus_);

    auto p = panel_.config();
    p.pin_cs = 42;
    p.pin_rst = 39;
    p.panel_width = 172;
    p.panel_height = 320;
    p.offset_x = 34;  // a RAM do ST7789 tem 240 colunas, o vidro usa as 172 do meio
    p.invert = true;
    p.readable = false;
    panel_.config(p);

    auto l = light_.config();
    l.pin_bl = 46;
    l.pwm_channel = 0;
    light_.config(l);
    panel_.setLight(&light_);
    setPanel(&panel_);
  }
};

inline Lcd lcd;

inline void displayBegin() {
  lcd.init();
  lcd.setBrightness(160);
}
inline void setBrightness(uint8_t v) { lcd.setBrightness(v * 160 / 255); }
inline void displaySetRotation(uint8_t rot) { lcd.setRotation(rot); }
inline int displayWidth() { return lcd.width(); }
inline int displayHeight() { return lcd.height(); }
inline void displayPush(const uint16_t* px, int y, int w, int h) {
  lcd.pushImage(0, y, w, h, reinterpret_cast<const lgfx::swap565_t*>(px));
}

constexpr int PIN_BUTTON = 0;  // BOOT
constexpr int PIN_LED = 38;    // RGB endereçável
inline Qmi8658 imu;            // I2C em SDA 48 / SCL 47, achado por varredura

inline void begin() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  imu.begin(Wire, 48, 47);
}

inline bool gravity(int16_t& x, int16_t& y) {
  int16_t z;
  return imu.accel(x, y, z);
}

inline void setLed(uint8_t r, uint8_t g, uint8_t b) {
  // o LED desta placa lê os bytes em ordem RGB; o neopixelWrite manda GRB (padrão WS2812)
  neopixelWrite(PIN_LED, g, r, b);
}

inline bool buttonPressed() { return digitalRead(PIN_BUTTON) == LOW; }
inline bool touch(int16_t&, int16_t&) { return false; }
inline bool touchRaw(int16_t&, int16_t&) { return false; }

}  // namespace board
