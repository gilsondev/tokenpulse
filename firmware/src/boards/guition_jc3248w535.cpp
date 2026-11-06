// A Arduino_GFX define macros de cor (PURPLE, GREEN…) que colidem com theme.h; fica só neste arquivo.
#if defined(BOARD_GUITION_JC3248W535)
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>

#include "guition_jc3248w535.h"

namespace board {
namespace {
Arduino_ESP32QSPI qspi(45 /* CS */, 47 /* SCK */, 21, 48, 40, 39);
Arduino_AXS15231B panel(&qspi, GFX_NOT_DEFINED, 0, false, 320, 480);

// O quadro mora na PSRAM. Mandado direto, o driver SPI copia cada bloco de 2 KB para um buffer
// temporário (31 ms por quadro); em faixas já na RAM interna o envio é uma transação por faixa.
constexpr int BAND_ROWS = 32;
constexpr int BACKLIGHT_CH = 0;
uint16_t* band = nullptr;
}  // namespace

void displayBegin() {
  // o Tessera trava CS e backlight com gpio_hold antes de reiniciar, e a trava sobrevive ao reset
  gpio_hold_dis(GPIO_NUM_45);
  gpio_hold_dis(GPIO_NUM_1);
  panel.begin();
  ledcSetup(BACKLIGHT_CH, 20000, 8);
  ledcAttachPin(PIN_BACKLIGHT, BACKLIGHT_CH);
  ledcWrite(BACKLIGHT_CH, 255);
  band = static_cast<uint16_t*>(heap_caps_aligned_alloc(16, 320 * BAND_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
}

void setBrightness(uint8_t v) { ledcWrite(BACKLIGHT_CH, v); }

void displaySetRotation(uint8_t rot) { landscape = rot & 1 && band; }

void displayPush(const uint16_t* px, int, int w, int h) {
  if (!band) return panel.draw16bitBeRGBBitmap(0, 0, const_cast<uint16_t*>(px), w, h);
  constexpr int NW = 320, NH = 480;  // o painel, sempre em retrato
  panel.startWrite();
  panel.writeAddrWindow(0, 0, NW, NH);
  for (int y = 0; y < NH; y += BAND_ROWS) {
    const int rows = min(BAND_ROWS, NH - y);
    if (!landscape) {
      memcpy(band, px + y * w, rows * w * 2);
    } else {  // o pixel (x, y) do retrato vem do (y, 319 - x) do quadro deitado
      for (int r = 0; r < rows; r++)
        for (int x = 0; x < NW; x++) band[r * NW + x] = px[(NW - 1 - x) * w + y + r];
    }
    qspi.writeBytes(reinterpret_cast<uint8_t*>(band), rows * NW * 2);
  }
  panel.endWrite();
}

}  // namespace board
#endif
