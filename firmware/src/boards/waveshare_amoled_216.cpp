#if defined(BOARD_WAVESHARE_AMOLED_216)
#include <SensorPCF85063.hpp>
#include <TouchDrvCSTXXX.hpp>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#include <driver/i2s.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <soc/gpio_struct.h>

extern "C" {
#include "es8311.h"
}

#include "../sound.h"
#include "waveshare_amoled_216.h"

namespace board {
namespace {
constexpr int N = PANEL, TP_INT = 11, TP_RST = 40;
TouchDrvCST92xx tp;
bool tpOk = false;

// CO5300 por QSPI, direto no spi_master do IDF (o mesmo formato da Arduino_GFX): comando numa linha
// (0x02, registrador no endereço) e pixels em 4 linhas (0x32, 0x3C00: continua a escrita na RAM). A Arduino_GFX
// segura o barramento e espera cada transação; aqui a faixa sai por DMA enquanto a próxima é montada.
constexpr int PIN_CS = 12, PIN_SCK = 38, PIN_D[4] = {4, 5, 6, 7}, PIN_RST = 39;
spi_device_handle_t dev = nullptr;
inline void csLow() { GPIO.out_w1tc = 1u << PIN_CS; }
inline void csHigh() { GPIO.out_w1ts = 1u << PIN_CS; }

void command(uint8_t reg, const uint8_t* data = nullptr, int n = 0) {
  spi_transaction_t t = {};
  t.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR | (n ? SPI_TRANS_USE_TXDATA : 0);
  t.cmd = 0x02;
  t.addr = (uint32_t)reg << 8;
  for (int i = 0; i < n; i++) t.tx_data[i] = data[i];
  t.length = n * 8;
  csLow();
  spi_device_polling_transmit(dev, &t);
  csHigh();
}
void command1(uint8_t reg, uint8_t v) { command(reg, &v, 1); }

// O quadro mora na PSRAM; vai em faixas de 32 linhas copiadas para a RAM interna, em duas faixas alternadas:
// uma transmite enquanto a outra é montada.
constexpr int BAND_ROWS = 32;
uint16_t* bands[2] = {nullptr, nullptr};
spi_transaction_ext_t xfer[2];
// O brilho é um comando no mesmo barramento do envio, que roda no outro núcleo: fica pendente até o próximo envio.
volatile int pendingBright = -1;
}  // namespace

void displayBegin() {
  pinMode(PIN_CS, OUTPUT);
  csHigh();
  spi_bus_config_t bus = {};
  bus.data0_io_num = PIN_D[0], bus.data1_io_num = PIN_D[1], bus.data2_io_num = PIN_D[2], bus.data3_io_num = PIN_D[3];
  bus.sclk_io_num = PIN_SCK;
  bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
  bus.max_transfer_sz = N * BAND_ROWS * 2 + 8;
  bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS;
  spi_device_interface_config_t cfg = {};
  cfg.command_bits = 8, cfg.address_bits = 24, cfg.mode = 0;
  cfg.clock_speed_hz = 80000000;  // pela matriz de GPIO o S3 entrega ~40 MHz nestes pinos
  cfg.spics_io_num = -1, cfg.flags = SPI_DEVICE_HALFDUPLEX, cfg.queue_size = 1;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK || spi_bus_add_device(SPI2_HOST, &cfg, &dev) != ESP_OK)
    return void(Serial.println("ERRO tela: QSPI não iniciou"));
  spi_device_acquire_bus(dev, portMAX_DELAY);

  pinMode(PIN_RST, OUTPUT);  // a sequência da Arduino_CO5300
  digitalWrite(PIN_RST, HIGH), delay(10), digitalWrite(PIN_RST, LOW), delay(200), digitalWrite(PIN_RST, HIGH), delay(200);
  command(0x11), delay(120);  // sai do sono
  command1(0xFE, 0x00);
  command1(0xC4, 0x80);  // modo SPI
  command1(0x3A, 0x55);  // 16 bits por pixel
  command1(0x53, 0x20);
  command1(0x63, 0xFF);  // brilho máximo do HBM
  command(0x29);         // liga a tela
  command1(0x51, 0xD0);
  command1(0x58, 0x00);  // sem realce de contraste
  command(0x20);         // sem inversão
  command1(0x36, 0x00);  // MADCTL: RGB, sem espelhar

  for (auto& b : bands) b = static_cast<uint16_t*>(heap_caps_aligned_alloc(16, N * BAND_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  for (int i = 0; i < 2; i++) {
    xfer[i] = {};
    xfer[i].base.length = N * BAND_ROWS * 2 * 8;
    xfer[i].base.tx_buffer = bands[i];
  }
  if (bands[0] && bands[1]) {  // tela preta antes do primeiro quadro
    memset(bands[0], 0, N * BAND_ROWS * 2);
    displayPush(nullptr, 0, 0, 0);
  }

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
extern const uint8_t _binary_data_sounds_power_up_7_pcm_start[], _binary_data_sounds_power_up_7_pcm_end[];
extern const uint8_t _binary_data_sounds_three_tone_1_pcm_start[], _binary_data_sounds_three_tone_1_pcm_end[];
extern const uint8_t _binary_data_sounds_pep_sound_1_pcm_start[], _binary_data_sounds_pep_sound_1_pcm_end[];
}

namespace {
constexpr int SR = 24000, PA_CTRL = 46;  // os PCM de data/sounds são de 24 kHz
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
    {_binary_data_sounds_bong_001_pcm_start, _binary_data_sounds_bong_001_pcm_end},
    {_binary_data_sounds_power_up_7_pcm_start, _binary_data_sounds_power_up_7_pcm_end},
    {_binary_data_sounds_three_tone_1_pcm_start, _binary_data_sounds_three_tone_1_pcm_end},
    {_binary_data_sounds_pep_sound_1_pcm_start, _binary_data_sounds_pep_sound_1_pcm_end}};
static_assert(sizeof CLIPS / sizeof *CLIPS == sound::COUNT, "um PCM por nome em sound.h");
QueueHandle_t queue = nullptr;
es8311_handle_t codec = nullptr;
struct Req {
  uint8_t i, vol, times;
  uint16_t minMs;
};

void soundTask(void*) {
  Req q;
  while (xQueueReceive(queue, &q, portMAX_DELAY) == pdTRUE) {
    es8311_voice_volume_set(codec, q.vol, nullptr);
    // repete com 150 ms de pausa até tocar `times` vezes e durar ao menos `minMs`
    const size_t bytes = CLIPS[q.i].end - CLIPS[q.i].start;
    const uint32_t clipMs = bytes / 2 * 1000 / SR + 150;
    static const int16_t silence[SR * 150 / 1000] = {};
    for (uint32_t n = 0, ms = 0; n < q.times || ms < q.minMs; n++, ms += clipMs) {
      size_t written;
      if (n) i2s_write(I2S_NUM_0, silence, sizeof silence, &written, portMAX_DELAY);
      i2s_write(I2S_NUM_0, CLIPS[q.i].start, bytes, &written, portMAX_DELAY);
    }
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

void play(uint8_t i, uint8_t vol, uint16_t minMs, uint8_t times) {
  const Req q{i, vol, max<uint8_t>(times, 1), minMs};
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

void displayPush(const uint16_t* px, int, int, int) {
  if (!dev || !bands[0] || !bands[1]) return;
  if (pendingBright >= 0) command1(0x51, pendingBright), pendingBright = -1;
  // AMOLED marca com imagem parada: a imagem inteira anda até 6 px, uma posição nova por minuto
  static const int8_t ORBIT[9][2] = {{0, 0}, {6, 0}, {6, 6}, {0, 6}, {-6, 6}, {-6, 0}, {-6, -6}, {0, -6}, {6, -6}};
  const auto& o = ORBIT[millis() / 60000 % 9];
  const int sx = PAD + o[0], sy = PAD + o[1], S = SIDE;
  const int x0 = max(0, sx), x1 = min(N, sx + S);  // colunas da tela com conteúdo
  const uint8_t win[4] = {0, 0, (N - 1) >> 8, (N - 1) & 0xFF};
  command(0x2A, win, 4), command(0x2B, win, 4), command(0x2C);  // a tela inteira; a escrita começa no canto
  const int32_t fill = pushFill;
  const uint32_t fill32 = fill < 0 ? 0 : (uint32_t)__builtin_bswap16(fill) * 0x10001u;
  csLow();
  for (int y = 0, k = 0; y < N; y += BAND_ROWS, k ^= 1) {
    uint16_t* band = bands[k];
    const int rows = min(BAND_ROWS, N - y);
    memset(band, 0, rows * N * 2);
    const int r0 = max(0, sy - y), r1 = min(rows, sy + S - y);  // linhas da faixa com conteúdo
    // o quadro girado mora na PSRAM: cada rotação lê em sequência (o cache dela é por linha), nunca em coluna
    if (px && r0 < r1) switch (rotation) {
        case 0:
          for (int r = r0; r < r1; r++) memcpy(band + r * N + x0, px + (y + r - sy) * S + (x0 - sx), (x1 - x0) * 2);
          break;
        case 2:
          for (int r = r0; r < r1; r++) {
            const uint16_t* src = px + (S - 1 - (y + r - sy)) * S + S - 1 - (x0 - sx);
            for (uint16_t *d = band + r * N + x0, *e = band + r * N + x1; d < e;) *d++ = *src--;
          }
          break;
        case 1:  // o ponto (cx, cy) vem de px[(S - 1 - cx) * S + cy]: uma coluna da tela é uma linha do quadro
          for (int x = x0; x < x1; x++) {
            const uint16_t* src = px + (S - 1 - (x - sx)) * S + (y + r0 - sy);
            for (int r = r0; r < r1; r++) band[r * N + x] = *src++;
          }
          break;
        default:  // 3: de px[cx * S + S - 1 - cy]
          for (int x = x0; x < x1; x++) {
            const uint16_t* src = px + (x - sx) * S + S - 1 - (y + r0 - sy);
            for (int r = r0; r < r1; r++) band[r * N + x] = *src--;
          }
      }
    if (y > 0) spi_device_polling_end(dev, portMAX_DELAY);  // a faixa anterior terminou de sair
    auto& x = xfer[k];
    x.base.length = rows * N * 2 * 8;
    // a primeira faixa leva o comando de continuar a escrita; as outras são só dados, com o CS ainda baixo
    x.base.flags = SPI_TRANS_MODE_QIO | (y ? SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY : 0);
    x.base.cmd = 0x32, x.base.addr = 0x003C00;
    spi_device_polling_start(dev, &x.base, portMAX_DELAY);
    // enquanto a faixa sai: limpa no quadro as linhas já copiadas, e o próximo desenho nele não precisa limpar
    if (px && fill >= 0) {
      const int cy0 = max(0, y - sy), cy1 = min(S, y + rows - sy);  // linhas do quadro que esta faixa leu
      if (rotation == 0 || rotation == 2) {
        const int f0 = rotation == 0 ? cy0 : S - cy1, f1 = rotation == 0 ? cy1 : S - cy0;
        auto* q = reinterpret_cast<uint32_t*>(const_cast<uint16_t*>(px) + f0 * S);
        for (int i = 0, n = (f1 - f0) * S / 2; i < n; i++) q[i] = fill32;
      } else {  // giradas: a faixa leu as colunas cy0..cy1 do quadro (espelhadas na 3), em todas as linhas
        const int c0 = rotation == 1 ? cy0 : S - cy1, c1 = rotation == 1 ? cy1 : S - cy0;
        uint16_t* q = const_cast<uint16_t*>(px);
        const uint16_t f16 = fill32;
        for (int r = 0; r < S; r++)
          for (uint16_t *d = q + r * S + c0, *e = q + r * S + c1; d < e;) *d++ = f16;
      }
    }
  }
  spi_device_polling_end(dev, portMAX_DELAY);
  csHigh();
}

// RTC PCF85063, com a hora local (a placa não sabe de fuso): o daemon manda a hora do Mac e ela fica guardada
// para o relógio funcionar desde o boot e sem o Mac.
namespace {
SensorPCF85063 rtc;
int rtcOk = -1;  // -1: ainda não iniciado
bool rtcReady() {
  if (rtcOk < 0) rtcOk = rtc.begin(Wire, PIN_SDA, PIN_SCL);
  return rtcOk;
}
}  // namespace

bool rtcRead(uint32_t& t) {
  if (!rtcReady() || !rtc.isClockIntegrityGuaranteed()) return false;  // ficou sem energia: a hora não vale
  struct tm tm = rtc.getDateTime().toUnixTime();
  tm.tm_isdst = 0;
  const time_t v = mktime(&tm);  // sem TZ definido, o mktime da placa é UTC: devolve a mesma hora "local"
  if (v < 1700000000) return false;
  t = v;
  return true;
}

void rtcWrite(uint32_t t) {
  if (!rtcReady()) return;
  const time_t v = t;
  struct tm tm;
  gmtime_r(&v, &tm);
  rtc.setDateTime(RTC_DateTime(tm));
}

// Bateria pelo AXP2101 (XPowersLib). A carga fica como a placa já está: tensão e corrente dependem da bateria.
namespace {
XPowersPMU pmu;
bool pmuOk = false;
}  // namespace

void pmuBegin() {
  pmuOk = pmu.init(Wire, PIN_SDA, PIN_SCL, AXP2101_SLAVE_ADDRESS);
  if (!pmuOk) return void(Serial.println("PMU AXP2101 sem resposta"));
  pmu.enableBattDetection();
  pmu.enableBattVoltageMeasure();
  pmu.enableVbusVoltageMeasure();
  // LiPo de 1000 mAh quase sempre na USB: parar em 4,1 V (em vez de 4,2 V) troca ~10% de carga por bem mais ciclos,
  // 200 mA é 0,2C (a PMU vem com 25 mA, ~40 h para encher) e o desligamento em 3,3 V evita descarga profunda
  pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V1);
  pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_200MA);
  pmu.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
  pmu.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
  pmu.setSysPowerDownVoltage(3300);
  Serial.printf("PMU: bateria %d, USB %d, carregando %d, %d%%, %u mV\n", pmu.isBatteryConnect(), pmu.isVbusIn(),
                pmu.isCharging(), pmu.getBatteryPercent(), pmu.getBattVoltage());
}

// A configuração de carga gravada na PMU, para o comando "pmu"
void pmuReport() {
  if (!pmuOk) return (void)Serial.println("pmu: sem resposta");
  static const char* VOL[] = {"?", "4,0", "4,1", "4,2", "4,35", "4,4"};
  static const char* ST[] = {"trickle", "pré-carga", "corrente constante", "tensão constante", "carregada", "parada"};
  const int cc = pmu.getChargerConstantCurr(), v = pmu.getChargeTargetVoltage(), st = pmu.getChargerStatus();
  Serial.printf("pmu: alvo %s V, corrente %d mA, término %d mA (%s), estado %s, bateria %u mV, %d%%, USB %d, VBUS %u mV, desliga em %u mV\n",
                v >= 1 && v <= 5 ? VOL[v] : "?", cc <= 4 ? cc * 25 : cc <= 8 ? 100 + (cc - 4) * 25 : 200 + (cc - 8) * 100,
                (int)pmu.getChargerTerminationCurr() * 25, pmu.isChargerTerminationLimit() ? "ligado" : "desligado",
                st >= 0 && st <= 5 ? ST[st] : "?", pmu.getBattVoltage(), pmu.getBatteryPercent(), pmu.isVbusIn(), pmu.getVbusVoltage(),
                pmu.getSysPowerDownVoltage());
}

// Lida a cada 2 s: o estado muda devagar e o I2C é dividido com o toque.
bool powerState(PowerState& s) {
  static PowerState last;
  static uint32_t lastMs = 0;
  if (!pmuOk) return false;
  if (!lastMs || millis() - lastMs > 2000) {
    lastMs = millis();
    last.battery = pmu.isBatteryConnect();
    last.usb = pmu.isVbusIn();
    last.charging = last.battery && pmu.isCharging();
    last.pct = last.battery ? constrain(pmu.getBatteryPercent(), 0, 100) : -1;
  }
  s = last;
  return true;
}

}  // namespace board
#endif
