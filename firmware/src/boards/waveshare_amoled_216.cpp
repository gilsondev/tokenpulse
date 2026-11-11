// A Arduino_GFX define macros de cor (PURPLE, GREEN…) que colidem com theme.h; fica só neste arquivo.
#if defined(BOARD_WAVESHARE_AMOLED_216)
#include <Arduino_GFX_Library.h>
#include <TouchDrvCSTXXX.hpp>
#include <driver/i2s.h>
#include <esp_heap_caps.h>

extern "C" {
#include "es8311.h"
}

#include "../sound.h"
#include "waveshare_amoled_216.h"

namespace board {
namespace {
constexpr int N = PANEL, TP_INT = 11, TP_RST = 40;
Arduino_ESP32QSPI qspi(12 /* CS */, 38 /* SCK */, 4, 5, 6, 7);
Arduino_CO5300 panel(&qspi, 39 /* RST */, 0, false, N, N);
TouchDrvCST92xx tp;
bool tpOk = false;

// O quadro mora na PSRAM; vai em faixas copiadas para a RAM interna, como na Guition.
constexpr int BAND_ROWS = 32;
uint16_t* band = nullptr;
// O brilho é um comando no mesmo barramento do envio, que roda no outro núcleo: fica pendente até o próximo envio.
volatile int pendingBright = -1;
}  // namespace

void displayBegin() {
  panel.begin();
  band = static_cast<uint16_t*>(heap_caps_aligned_alloc(16, N * BAND_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  panel.fillScreen(0);

  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);
  delay(10);
  digitalWrite(TP_RST, HIGH);
  delay(50);
  tp.setPins(-1, TP_INT);
  tpOk = tp.begin(Wire, 0x5A, PIN_SDA, PIN_SCL);
  if (tpOk) tp.setMaxCoordinates(N, N);
  Serial.printf("touch CST92xx: %s\n", tpOk ? "ok" : "sem resposta");
}

void setBrightness(uint8_t v) { pendingBright = v; }

// Som: ES8311 por I2S. A configuração do I2S e do codec vem do claude-desktop-buddy-esp32 (MIT,
// Copyright 2026 Anthropic, PBC): https://github.com/vthinkxie/claude-desktop-buddy-esp32
extern "C" {
extern const uint8_t _binary_data_sounds_question_001_pcm_start[], _binary_data_sounds_question_001_pcm_end[];
extern const uint8_t _binary_data_sounds_question_002_pcm_start[], _binary_data_sounds_question_002_pcm_end[];
extern const uint8_t _binary_data_sounds_question_003_pcm_start[], _binary_data_sounds_question_003_pcm_end[];
extern const uint8_t _binary_data_sounds_question_004_pcm_start[], _binary_data_sounds_question_004_pcm_end[];
extern const uint8_t _binary_data_sounds_confirmation_001_pcm_start[], _binary_data_sounds_confirmation_001_pcm_end[];
extern const uint8_t _binary_data_sounds_confirmation_002_pcm_start[], _binary_data_sounds_confirmation_002_pcm_end[];
extern const uint8_t _binary_data_sounds_glass_001_pcm_start[], _binary_data_sounds_glass_001_pcm_end[];
extern const uint8_t _binary_data_sounds_bong_001_pcm_start[], _binary_data_sounds_bong_001_pcm_end[];
}

namespace {
constexpr int SR = 16000, PA_CTRL = 46;
struct Clip {
  const uint8_t *start, *end;
};
const Clip CLIPS[] = {
    {_binary_data_sounds_question_001_pcm_start, _binary_data_sounds_question_001_pcm_end},
    {_binary_data_sounds_question_002_pcm_start, _binary_data_sounds_question_002_pcm_end},
    {_binary_data_sounds_question_003_pcm_start, _binary_data_sounds_question_003_pcm_end},
    {_binary_data_sounds_question_004_pcm_start, _binary_data_sounds_question_004_pcm_end},
    {_binary_data_sounds_confirmation_001_pcm_start, _binary_data_sounds_confirmation_001_pcm_end},
    {_binary_data_sounds_confirmation_002_pcm_start, _binary_data_sounds_confirmation_002_pcm_end},
    {_binary_data_sounds_glass_001_pcm_start, _binary_data_sounds_glass_001_pcm_end},
    {_binary_data_sounds_bong_001_pcm_start, _binary_data_sounds_bong_001_pcm_end}};
static_assert(sizeof CLIPS / sizeof *CLIPS == sound::COUNT, "um PCM por nome em sound.h");
QueueHandle_t queue = nullptr;
es8311_handle_t codec = nullptr;
struct Req {
  uint8_t i, vol;
};

void soundTask(void*) {
  Req q;
  while (xQueueReceive(queue, &q, portMAX_DELAY) == pdTRUE) {
    es8311_voice_volume_set(codec, q.vol, nullptr);
    size_t written;
    i2s_write(I2S_NUM_0, CLIPS[q.i].start, CLIPS[q.i].end - CLIPS[q.i].start, &written, portMAX_DELAY);
    i2s_zero_dma_buffer(I2S_NUM_0);  // sem isso o fim do DMA repete e vira um zumbido
  }
}
}  // namespace

void audioBegin() {
  pinMode(PA_CTRL, OUTPUT);
  digitalWrite(PA_CTRL, HIGH);
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = SR;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = SR * 256;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  i2s_pin_config_t pins = {};
  pins.mck_io_num = 42;
  pins.bck_io_num = 9;
  pins.ws_io_num = 45;
  pins.data_out_num = 8;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK || i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK)
    return void(Serial.println("som: I2S não iniciou"));
  es8311_handle_t es = codec = es8311_create(0, ES8311_ADDRRES_0);
  const es8311_clock_config_t clk = {.mclk_inverted = false, .sclk_inverted = false, .mclk_from_mclk_pin = true,
                                     .mclk_frequency = SR * 256, .sample_frequency = SR};
  if (!es || es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK)
    return void(Serial.println("som: ES8311 sem resposta"));
  es8311_voice_volume_set(es, 60, nullptr);
  queue = xQueueCreate(4, sizeof(Req));
  xTaskCreatePinnedToCore(soundTask, "som", 4096, nullptr, 5, nullptr, 0);
}

void play(uint8_t i, uint8_t vol) {
  const Req q{i, vol};
  if (queue && i < sound::COUNT) xQueueSend(queue, &q, 0);
}

bool touchNative(int16_t& x, int16_t& y) {
  static uint32_t lastMs = 0;
  static bool down = false;
  static int16_t lx = 0, ly = 0;
  if (tpOk && millis() - lastMs >= 15) {
    lastMs = millis();
    int16_t xs[1], ys[1];
    down = tp.getPoint(xs, ys, 1) > 0;
    if (down) lx = xs[0], ly = ys[0];
  }
  x = lx, y = ly;
  return down;
}

uint16_t pixel(const uint16_t* px, int x, int y) {  // o ponto (x, y) do quadrado do meio vem deste pixel do quadro girado
  constexpr int S = SIDE;
  switch (rotation) {
    case 1: return px[(S - 1 - x) * S + y];
    case 2: return px[(S - 1 - y) * S + S - 1 - x];
    case 3: return px[x * S + S - 1 - y];
    default: return px[y * S + x];
  }
}

void displayPush(const uint16_t* px, int, int, int) {
  if (pendingBright >= 0) panel.setBrightness(pendingBright), pendingBright = -1;
  // AMOLED marca com imagem parada: a imagem inteira anda até 6 px, uma posição nova por minuto
  static const int8_t ORBIT[9][2] = {{0, 0}, {6, 0}, {6, 6}, {0, 6}, {-6, 6}, {-6, 0}, {-6, -6}, {0, -6}, {6, -6}};
  const auto& o = ORBIT[millis() / 60000 % 9];
  const int sx = PAD + o[0], sy = PAD + o[1];
  if (!band) return;
  panel.startWrite();
  panel.writeAddrWindow(0, 0, N, N);
  for (int y = 0; y < N; y += BAND_ROWS) {
    const int rows = min(BAND_ROWS, N - y);
    for (int r = 0; r < rows; r++) {
      uint16_t* out = band + r * N;
      const int ny = y + r - sy;
      if (ny < 0 || ny >= SIDE) {
        memset(out, 0, N * 2);
        continue;
      }
      for (int x = 0; x < N; x++) {
        const int nx = x - sx;
        out[x] = nx < 0 || nx >= SIDE ? 0 : pixel(px, nx, ny);
      }
    }
    qspi.writeBytes(reinterpret_cast<uint8_t*>(band), rows * N * 2);
  }
  panel.endWrite();
}

}  // namespace board
#endif
