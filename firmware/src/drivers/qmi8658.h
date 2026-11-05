#pragma once
#include <Wire.h>

// Acelerômetro do QMI8658, comum nas placas da Waveshare. Só o necessário para achar a gravidade.
class Qmi8658 {
 public:
  bool begin(TwoWire& wire, int sda, int scl, uint8_t addr = 0x6B) {
    wire_ = &wire;
    addr_ = addr;
    wire.begin(sda, scl, 400000);
    write(0x02, 0x40);  // CTRL1: auto-incremento de endereço, little-endian
    write(0x03, 0x05);  // CTRL2: acelerômetro ±2 g
    write(0x08, 0x01);  // CTRL7: liga o acelerômetro
    uint8_t who = 0;
    ok_ = read(0x00, &who, 1) && who == 0x05;
    return ok_;
  }

  bool accel(int16_t& x, int16_t& y, int16_t& z) {
    uint8_t b[6];
    if (!ok_ || !read(0x35, b, 6)) return false;
    x = b[0] | b[1] << 8, y = b[2] | b[3] << 8, z = b[4] | b[5] << 8;
    return true;
  }

 private:
  TwoWire* wire_ = nullptr;
  uint8_t addr_ = 0;
  bool ok_ = false;

  void write(uint8_t reg, uint8_t val) {
    wire_->beginTransmission(addr_);
    wire_->write(reg);
    wire_->write(val);
    wire_->endTransmission();
  }

  bool read(uint8_t reg, uint8_t* out, uint8_t n) {
    wire_->beginTransmission(addr_);
    wire_->write(reg);
    wire_->endTransmission(false);
    if (wire_->requestFrom(addr_, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) out[i] = wire_->read();
    return true;
  }
};
