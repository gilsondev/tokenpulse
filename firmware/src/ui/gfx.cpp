#include "gfx.h"

#include "fonts/inter_hero.h"
#include "fonts/inter_label.h"
#include "fonts/inter_num.h"
#include "fonts/inter_small.h"
#include "fonts/inter_title.h"
#include "fonts/inter_hero_m.h"
#include "fonts/inter_label_m.h"
#include "fonts/inter_num_m.h"
#include "fonts/inter_small_m.h"
#include "fonts/inter_title_m.h"

namespace gfx {
namespace {
// Dois canvas em memória: enquanto o outro núcleo envia um quadro para a tela, o próximo é desenhado
// no outro. Sem memória para o segundo, desenha e envia em série.
LGFX_Sprite canvas[2];
int cur = 0;
bool dual = false;
const uint16_t* lastShown = nullptr;  // o último quadro entregue à tela (só sem faixas)
QueueHandle_t pushQueue = nullptr;
SemaphoreHandle_t pushDone = nullptr;
struct Job {
  const uint16_t* px;
  int y, w, h;
};

// Sem PSRAM o quadro inteiro não cabe num bloco da RAM interna (240x320 são 150 KB; o maior bloco
// livre tem ~110 KB). Aí ele é desenhado e enviado em faixas de meia altura: cada faixa é um buffer
// separado e o canvas enxerga a tela inteira, recortado nas linhas da faixa.
int bands = 1, bandRows = 0, rowLo = 0, rowHi = 0, fullW = 0, fullH = 0;
uint16_t* bandBuf[2] = {nullptr, nullptr};
bool capture = false;  // o próximo quadro sai também pela serial (screenshot)

LGFX_Sprite& S() { return canvas[cur]; }

void pushTask(void*) {
  Job j;
  for (;;) {
    xQueueReceive(pushQueue, &j, portMAX_DELAY);
    board::displayPush(j.px, j.y, j.w, j.h);
    xSemaphoreGive(pushDone);
    // o próximo quadro costuma já estar na fila: sem esta pausa a tarefa nunca bloqueia, a IDLE0 não
    // roda e o watchdog reinicia a placa (task_wdt: IDLE0)
    vTaskDelay(1);
  }
}
uint8_t currentRotation = 0;
bool canvasOk = false;
float uiScale = 1;
Ui uiClass = COMPACT;

// Transição: o último quadro mostrado fica em `prev`; durante SLIDE_MS o quadro novo entra por cima.
constexpr uint32_t SLIDE_MS = 280;
uint16_t *prev = nullptr, *out = nullptr;
int slideDir = 0;
uint32_t slideMs = 0;

struct Vlw {
  const uint8_t* data;
  lgfx::PointerWrapper wrap;
  lgfx::VLWfont font;
};
Vlw vlw[] = {{inter_small}, {inter_label}, {inter_title}, {inter_num}, {inter_hero}};
Vlw vlwM[] = {{inter_small_m}, {inter_label_m}, {inter_title_m}, {inter_num_m}, {inter_hero_m}};  // UI média
bool vlwOk = false;

// O canvas tem largura × altura × 2 bytes. Tenta a PSRAM, depois a RAM interna, depois duas faixas.
bool allocCanvas(int w, int h) {
  for (auto& c : canvas) c.deleteSprite(), c.setColorDepth(16);
  for (auto& b : bandBuf) free(b), b = nullptr;
  fullW = w, fullH = h, bands = 1, bandRows = h, cur = 0;
  bool ok = false;
  for (bool psram : {true, false}) {
    canvas[0].setPsram(psram);
    if ((ok = canvas[0].createSprite(w, h))) break;
  }
  if (ok) {
    canvas[1].setPsram(true);
    dual = canvas[1].createSprite(w, h);
    return true;
  }
  bands = 2, bandRows = (h + 1) / 2;
  for (auto& b : bandBuf) b = static_cast<uint16_t*>(heap_caps_malloc(w * bandRows * 2, MALLOC_CAP_DMA));
  dual = bandBuf[1];
  return bandBuf[0];
}

void beginBand(int b) {
  rowLo = b * bandRows, rowHi = min(fullH, rowLo + bandRows);
  if (bands == 1) return;
  // o ponteiro recua até a linha 0: as coordenadas continuam as da tela, e o recorte impede escrever
  // fora das linhas que existem
  canvas[cur].setBuffer(bandBuf[cur] - rowLo * fullW, fullW, fullH, lgfx::rgb565_2Byte);
  canvas[cur].setClipRect(0, rowLo, fullW, rowHi - rowLo);
}

void setFont(Font f) {
  if (full() && vlwOk) return S().setFont(&(uiClass == MEDIUM ? vlwM : vlw)[f].font);
  static const lgfx::IFont* SMALL_SCREEN[] = {&fonts::efontJA_10, &fonts::efontJA_12, &fonts::efontJA_12,
                                              &fonts::FreeSansBold12pt7b, &fonts::FreeSansBold12pt7b};
  S().setFont(SMALL_SCREEN[f]);
}
}  // namespace

void begin() {
  color::apply(true);
  board::displayBegin();
  pushQueue = xQueueCreate(1, sizeof(Job));
  pushDone = xSemaphoreCreateBinary();
  xSemaphoreGive(pushDone);
  xTaskCreatePinnedToCore(pushTask, "push", 4096, nullptr, 2, nullptr, 0);
  setRotation(board::DEFAULT_ROTATION);
  vlwOk = true;
  for (auto* set : {vlw, vlwM})
    for (int i = 0; i < 5; i++) {
      set[i].wrap.set(set[i].data);
      vlwOk &= set[i].font.loadFont(&set[i].wrap);
    }
  if (!vlwOk) Serial.println("ERRO fontes VLW: usando as bitmap");
}

void setRotation(uint8_t rot) {
  // o envio no outro núcleo usa os canvas e o barramento da tela: espera ele terminar antes de mexer
  const bool locked = pushDone && xSemaphoreTake(pushDone, pdMS_TO_TICKS(1000));
  board::displaySetRotation(rot);
  currentRotation = rot;
  const int w = board::displayWidth(), h = board::displayHeight();
  uiScale = max(1.f, min(min(w, h) / 172.f, max(w, h) / 320.f));
  const int side = min(w, h);
  uiClass = !board::PROFILE.touch || side < 220 ? COMPACT : side < 300 ? MEDIUM : LARGE;
  canvasOk = allocCanvas(w, h);
  if (!canvasOk) Serial.printf("ERRO sem memória para o canvas %dx%d\n", w, h);
  beginBand(0);
  free(prev), free(out);
  prev = (uint16_t*)ps_malloc(w * h * 2);
  out = (uint16_t*)ps_malloc(w * h * 2);
  if (!prev || !out) free(prev), free(out), prev = out = nullptr;  // sem PSRAM: troca de tela sem deslizar
  lastShown = nullptr, slideDir = 0;  // o quadro mostrado era de um canvas que acabou de ser refeito
  if (locked) xSemaphoreGive(pushDone);
}

bool ready() { return canvasOk; }

void clear(uint16_t c) {
  const uint32_t sw = __builtin_bswap16(c), v = sw | sw << 16;
  auto* p = reinterpret_cast<uint32_t*>(static_cast<uint16_t*>(S().getBuffer()) + rowLo * fullW);
  for (int i = 0, n = (rowHi - rowLo) * fullW / 2; i < n; i++) p[i] = v;
}
void rows(int& lo, int& hi) { lo = rowLo, hi = rowHi; }
void captureNext() { capture = true; }

void wideLine(float x0, float y0, float x1, float y1, float r, uint16_t c) {
  if (bands == 1) return S().drawWideLine(x0, y0, x1, y1, r, c);
  // um sprite que só tem as linhas da faixa: o recorte da LovyanGFX para nele
  static LGFX_Sprite view;
  view.setBuffer(bandBuf[cur], fullW, rowHi - rowLo, lgfx::rgb565_2Byte);
  view.drawWideLine(x0, y0 - rowLo, x1, y1 - rowLo, r, c);
}
float scale() { return uiScale; }
Ui ui() { return uiClass; }
uint8_t rotation() { return currentRotation; }
LGFX_Sprite& fb() { return S(); }

const uint16_t* shown() { return lastShown; }

void slide(int dir) {
  if (!prev || !lastShown) return;
  memcpy(prev, lastShown, S().width() * S().height() * 2);  // a foto do que está na tela agora
  slideDir = dir;
  slideMs = millis();
}

namespace {
void presentBand() {
  const int w = fullW, h = rowHi - rowLo;
  auto* frame = static_cast<uint16_t*>(S().getBuffer()) + rowLo * w;
  // o quadro anterior terminou de ir para a tela; se o envio travou (QSPI pendurado), reinicia
  if (!xSemaphoreTake(pushDone, pdMS_TO_TICKS(2000))) {
    Serial.println("ERRO envio da tela travado: reiniciando");
    ESP.restart();
  }
  float t = slideDir ? (millis() - slideMs) / (float)SLIDE_MS : 1;
  if (t >= 1) {
    slideDir = 0;
  } else {
    float e = 1 - powf(1 - t, 3);    // ease-out cúbico
    int off = lroundf(w * (1 - e));  // quanto do quadro novo ainda falta entrar
    if (slideDir == UP) {  // a tela atual sobe e revela a nova por baixo, como o gesto de início do celular
      const int shift = h - lroundf(h * (1 - e));  // quanto a tela antiga já subiu
      for (int y = 0; y < h; y++)
        memcpy(out + y * w, y + shift < h ? prev + (y + shift) * w : frame + y * w, w * 2);
    } else {
      for (int y = 0; y < h; y++) {
        uint16_t *o = out + y * w, *p = prev + y * w, *c = frame + y * w;
        if (slideDir < 0) {  // novo entra pela direita
          memcpy(o, p + (w - off), off * 2);
          memcpy(o + off, c, (w - off) * 2);
        } else {
          memcpy(o, c + off, (w - off) * 2);
          memcpy(o + (w - off), p, off * 2);
        }
      }
    }
    frame = out;
  }
  lastShown = bands == 1 ? frame : nullptr;
  if (capture) {  // screenshot: o mesmo formato do comando shot, faixa por faixa
    if (rowLo == 0) Serial.printf("SHOT %d %d %d\n", w, fullH, w * fullH * 2);
    // em blocos com pausa: a 460800 baud direto, a ponte USB-serial perde alguns bytes do quadro
    const auto* b = reinterpret_cast<const uint8_t*>(frame);
    for (int i = 0, n = w * h * 2; i < n; i += 1024) Serial.write(b + i, min(1024, n - i)), Serial.flush(), delay(1);
    if (rowHi == fullH) Serial.flush(), capture = false;
  }
  if (!dual) {
    board::displayPush(frame, rowLo, w, h);
    xSemaphoreGive(pushDone);
    return;
  }
  Job j{frame, rowLo, w, h};
  xQueueSend(pushQueue, &j, portMAX_DELAY);
  cur ^= 1;  // o próximo quadro (ou faixa) é desenhado no outro canvas
}
}  // namespace

void frame(const std::function<void()>& draw) {
  if (!canvasOk) return;
  for (int b = 0; b < bands; b++) {
    beginBand(b);
    draw();
    presentBand();
  }
  beginBand(0);
}

namespace {
// Texto suave custa ~1 ms por linha (cada pixel do glifo é misturado com o fundo). As telas repetem
// os mesmos textos a cada quadro, então o texto pronto fica guardado e volta como cópia de bloco.
struct CachedText {
  uint32_t key = 0, usedMs = 0;
  LGFX_Sprite spr;
  int ax = 0, ay = 0;  // ponto de ancoragem do datum dentro do sprite
};
CachedText textCache[40];
constexpr int PAD = 3;

uint32_t textKey(const char* s, Font f, uint16_t c, uint8_t datum, uint16_t bg) {
  uint32_t h = 2166136261u;  // FNV-1a do texto; o resto entra pelo hashOf
  for (const char* p = s; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  return hashOf({h, (uint32_t)f, c, datum, bg});
}

// Copia o sprite para o canvas. Com `skip`, pula os pixels dessa cor (já trocada de byte): o fundo
// do texto pronto não cobre o que já foi desenhado embaixo.
void blit(LGFX_Sprite& from, int x, int y, int32_t skip = -1) {
  auto* src = static_cast<uint16_t*>(from.getBuffer());
  auto* dst = static_cast<uint16_t*>(S().getBuffer());
  const int W = fullW, w = from.width(), h = from.height();
  const int x0 = max(0, x), x1 = min(W, x + w);
  if (x1 <= x0) return;
  for (int r = max(0, rowLo - y); r < h && y + r < rowHi; r++) {
    uint16_t *d = dst + (y + r) * W + x0, *s = src + r * w + (x0 - x);
    if (skip < 0) {
      memcpy(d, s, (x1 - x0) * 2);
      continue;
    }
    for (int i = 0; i < x1 - x0; i++)
      if (s[i] != skip) d[i] = s[i];
  }
}
}  // namespace

// Em faixas a região pode cruzar a divisa e só metade dela existe: sem cache, desenha sempre.
bool RegionCache::restore(uint32_t k, int rx, int ry, int rw, int rh) {
  if (bands > 1) return false;
  const bool same = k == key && rx == x && ry == y && rw == w && rh == h;
  x = rx, y = ry, w = rw, h = rh;
  if (same) blit(spr, x, y);
  return same;
}

void RegionCache::store(uint32_t k) {
  if (bands > 1) return;
  if (spr.width() != w || spr.height() != h) {
    spr.deleteSprite();
    spr.setColorDepth(16);
    spr.setPsram(true);
    if (!spr.createSprite(w, h)) return void(key = 0);
  }
  auto* src = static_cast<uint16_t*>(S().getBuffer());
  auto* dst = static_cast<uint16_t*>(spr.getBuffer());
  for (int r = 0; r < h; r++) memcpy(dst + r * w, src + (y + r) * S().width() + x, w * 2);
  key = k;
}

uint32_t hashOf(std::initializer_list<uint32_t> vals) {
  uint32_t h = 2166136261u;
  for (uint32_t v : vals) h = (h ^ v) * 16777619u;
  return h | 1;
}

void text(const char* s, int x, int y, Font f, uint16_t c, uint8_t datum, uint16_t bg) {
  if (!s) return;  // campo ausente no frame: nada a desenhar
  setFont(f);
  // sem PSRAM não há cache: o texto pronto (até ~40 KB num número grande) disputaria a RAM com o frame
  if (!full() || !vlwOk || !*s || !psramFound()) {
    S().setTextDatum(datum);
    S().setTextColor(c);
    S().drawString(s, x, y);
    return;
  }
  const uint32_t key = textKey(s, f, c, datum, bg);
  CachedText* hit = nullptr;
  CachedText* oldest = &textCache[0];
  for (auto& e : textCache) {
    if (e.key == key) hit = &e;
    if (e.usedMs < oldest->usedMs) oldest = &e;
  }
  if (!hit) {
    hit = oldest;
    // folga de meia linha em cima e embaixo: acentos e descendentes passam da altura da fonte
    // textWidth da LovyanGFX erra para menos em textos com acento ou "·" (perde o último caractere),
    // então o sprite tem uma linha de folga de cada lado; a cópia pula o fundo e a folga não cobre nada
    const int fh = S().fontHeight(), padX = PAD + fh, w = S().textWidth(s) + 2 * padX, h = 2 * fh + 2 * PAD;
    hit->spr.deleteSprite();
    hit->spr.setColorDepth(16);
    hit->spr.setPsram(true);
    if (!hit->spr.createSprite(w, h)) {
      hit->key = 0;
      S().setTextDatum(datum);
      S().setTextColor(c);
      S().drawString(s, x, y);
      return;
    }
    const int hz = datum & 3, vt = datum & ~3;  // horizontal: 0 esquerda, 1 centro, 2 direita
    hit->ax = hz == 1 ? w / 2 : hz == 2 ? w - padX : padX;
    hit->ay = vt == middle_left ? h / 2 : vt == bottom_left ? h - PAD - fh / 2 : PAD + fh / 2;
    hit->spr.fillScreen(bg);
    hit->spr.setFont(&(uiClass == MEDIUM ? vlwM : vlw)[f].font);
    hit->spr.setTextDatum(datum);
    hit->spr.setTextColor(c);
    hit->spr.drawString(s, hit->ax, hit->ay);
    hit->key = key;
  }
  hit->usedMs = millis();
  blit(hit->spr, x - hit->ax, y - hit->ay, __builtin_bswap16(bg));
}

int textWidth(const char* s, Font f) {
  setFont(f);
  return S().textWidth(s);
}

int lineHeight(Font f) {
  setFont(f);
  return S().fontHeight();
}

void bar(int x, int y, int w, int h, float frac, uint16_t c, float shine, float pace, float halo) {
  frac = constrain(frac, 0.f, 1.f);
  if (!full()) {  // tela pequena: barra simples com o brilho periódico
    S().fillRoundRect(x, y, w, h, 2, color::TRACK);
    int fw = w * frac;
    if (fw < 5) return;
    S().fillRoundRect(x, y, fw, h, 2, c);
    int sx = x - 12 + (millis() % 2400) * (fw + 24) / 2400;
    for (int i = 0; i < 12; i++)
      if (sx + i > x + 2 && sx + i < x + fw - 2) S().drawFastVLine(sx + i, y + 1, h - 2, blend(c, color::TEXT, 0.5f * (1 - abs(i - 6) / 6.f)));
    return;
  }
  const int r = h / 2;
  S().fillSmoothRoundRect(x, y, w, h, r, color::TRACK);
  if (frac <= 0.002f) return;
  const int fw = max(h, (int)lroundf(w * frac));  // valor pequeno ainda aparece como um ponto
  const uint16_t from = blend(c, color::BG, 0.45f), head = blend(c, color::TEXT, 0.25f);
  // halo: o trilho logo depois da ponta acende na cor da barra, sumindo em 10 px
  for (int d = 1; d <= 10 && halo > 0; d++) {
    const int col = x + fw + d - 1;
    if (col >= x + w - r) break;
    const float a = halo * (1 - d / 10.f) * (1 - d / 10.f);
    S().drawFastVLine(col, y, h, blend(color::TRACK, c, a));
  }
  S().fillSmoothRoundRect(x, y, fw, h, r, from);
  for (int i = r; i < fw - r; i++)  // gradiente: mais apagado no começo, cheio perto da ponta
    S().drawFastVLine(x + i, y, h, blend(from, c, powf((float)i / fw, 0.8f)));
  S().fillSmoothRoundRect(x + fw - h, y, h, h, r, head);
  auto* buf = static_cast<uint16_t*>(S().getBuffer());
  const int W = S().width();
  // listras andando no trecho que passou do ritmo da semana
  const int px0 = pace >= 0 ? x + (int)(w * pace) : x + fw;
  const int off = millis() * 8 / 1000;
  for (int col = max(px0, x + r); col < x + fw - r; col++)
    for (int row = max(y + 1, rowLo); row < min(y + h - 1, rowHi); row++)
      if (((col - row + off) & 7) < 3) {
        uint16_t& p = buf[row * W + col];
        p = __builtin_bswap16(blend(__builtin_bswap16(p), color::TEXT, 0.14f));
      }
  // brilho único depois de uma mudança: uma faixa clara atravessa e desacelera
  if (shine >= 0 && shine < 1) {
    const float e = 1 - powf(1 - shine, 3);
    const int sx = x - 16 + (int)(e * (fw + 32));
    for (int i = 0; i < 16; i++) {
      const int col = sx + i;
      if (col <= x + r || col >= x + fw - r) continue;
      const float a = 0.55f * (1 - abs(i - 8) / 8.f) * (1 - shine);
      for (int row = max(y, rowLo); row < min(y + h, rowHi); row++) {
        uint16_t& p = buf[row * W + col];
        p = __builtin_bswap16(blend(__builtin_bswap16(p), color::TEXT, a));
      }
    }
  }
}

namespace {
// atan2 em [0, 2π) a partir de 12 h no sentido horário; erro máximo ~0,005 rad, bem abaixo de um pixel no anel
inline float clockAngle(float dx, float dy) {
  float y = dx, x = -dy, ax = fabsf(x), ay = fabsf(y);
  float a = min(ax, ay) / max(max(ax, ay), 1e-6f), s2 = a * a;
  float r = ((-0.0464964749f * s2 + 0.15931422f) * s2 - 0.327622764f) * s2 * a + a;
  if (ay > ax) r = TAU / 4 - r;
  if (x < 0) r = TAU / 2 - r;
  if (y < 0) r = TAU - r;
  return r;
}
}  // namespace

void ring(int cx, int cy, int r, int thick, float frac, uint16_t c, uint16_t track, uint16_t bg) {
  frac = constrain(frac, 0.f, 1.f);
  const float rOut = r, rIn = r - thick, rMid = r - thick / 2.f, cap = thick / 2.f;
  const float sweep = frac * TAU;
  // centros das pontas: 12 h e o fim do arco
  const float ax = cx, ay = cy - rMid, bx = cx + rMid * sinf(sweep), by = cy - rMid * cosf(sweep);
  auto cov = [](float d) { return constrain(0.5f - d, 0.f, 1.f); };  // d: distância com sinal até a borda
  auto capCov = [&](int x, int y, float px, float py) {
    float dx = x - px, dy = y - py;
    return fabsf(dx) > cap + 1 || fabsf(dy) > cap + 1 ? 0.f : cov(sqrtf(dx * dx + dy * dy) - cap);
  };
  auto* buf = static_cast<uint16_t*>(S().getBuffer());
  const int W = fullW;
  // gradiente ao longo do arco: apagado em 12 h, cheio na ponta
  uint16_t lut[64];
  for (int i = 0; i < 64; i++) lut[i] = blend(blend(c, bg, 0.4f), c, i / 63.f);
  const uint16_t tSw = __builtin_bswap16(track);  // o sprite guarda o byte alto primeiro
  for (int y = max(rowLo, cy - r - 1); y <= min(rowHi - 1, cy + r + 1); y++) {
    const float dy = y - cy;
    // só a faixa do anel nesta linha: entre o círculo de fora e o buraco de dentro
    const int xo = (int)sqrtf(max(0.f, (rOut + 1) * (rOut + 1) - dy * dy)) + 1;
    const int xi = rIn > 1 && fabsf(dy) < rIn - 1 ? (int)sqrtf((rIn - 1) * (rIn - 1) - dy * dy) : 0;
    for (int side = -1; side <= 1; side += 2) {
      for (int k = xi; k <= xo; k++) {
        const int x = cx + side * k;
        if (x < 0 || x >= W || (side > 0 && k == 0)) continue;
        const float dx = x - cx, d = sqrtf(dx * dx + dy * dy);
        const float annulus = min(cov(d - rOut), cov(rIn - d));
        if (annulus <= 0) continue;
        float arc = 0;
        int li = 63;
        if (frac > 0) {
          const float a = clockAngle(dx, dy);
          if (a <= sweep || frac >= 1) arc = annulus, li = min(63, (int)(a / max(sweep, 1e-3f) * 63));
          if (arc < 1 && frac < 1) {
            const float ca = capCov(x, y, ax, ay), cb = capCov(x, y, bx, by);
            if (ca > arc) arc = ca, li = 0;
            if (cb > arc) arc = cb, li = 63;
          }
        }
        uint16_t& px = buf[y * W + x];
        if (annulus >= 1 && arc <= 0) px = tSw;  // miolo do trilho: sem mistura
        else if (annulus >= 1 && arc >= 1) px = __builtin_bswap16(lut[li]);
        else px = __builtin_bswap16(blend(blend(bg, track, annulus), lut[li], arc));
      }
    }
  }
}

void ringHalo(int cx, int cy, int r, int thick, float frac, uint16_t c, float alpha) {
  if (frac <= 0 || frac >= 1 || alpha <= 0) return;
  const float rMid = r - thick / 2.f, cap = thick / 2.f, sweep = frac * TAU, R = 12;
  const float hx = cx + rMid * sinf(sweep), hy = cy - rMid * cosf(sweep);
  auto* buf = static_cast<uint16_t*>(S().getBuffer());
  const int W = S().width();
  for (int y = max(rowLo, (int)(hy - cap - R)); y <= min(rowHi - 1, (int)(hy + cap + R)); y++)
    for (int x = (int)(hx - cap - R); x <= (int)(hx + cap + R); x++) {
      const float dx = x - cx, dy = y - cy, d = sqrtf(dx * dx + dy * dy);
      if (d < r - thick + 0.5f || d > r - 0.5f) continue;  // só o miolo do trilho
      if (clockAngle(dx, dy) <= sweep) continue;
      const float e = sqrtf((x - hx) * (x - hx) + (y - hy) * (y - hy)) - cap;
      if (e <= 0 || e >= R) continue;
      const float a = alpha * (1 - e / R) * (1 - e / R);
      uint16_t& p = buf[y * W + x];
      p = __builtin_bswap16(blend(__builtin_bswap16(p), c, a));
    }
}

void loading(Icon icon, const char* title, const char* status, float progress, uint16_t c) {
  const bool logo = icon == BOOT;  // o boot mostra o logo: anel grosso com a linha de pulso no meio
  const int cx = width() / 2, cy = height() / 2 - s(20), r = s(logo ? 44 : 34), u = s(12);
  const float lw = max(1.f, scale() * 1.4f);
  frame([&] {
  clear();
  ring(cx, cy, r, logo ? r / 4 : s(5), progress, logo ? color::ACCENT : c, color::TRACK);
  auto line = [&](float x0, float y0, float x1, float y1) {
    wideLine(cx + x0 * u, cy + y0 * u, cx + x1 * u, cy + y1 * u, lw, color::TEXT);
  };
  switch (icon) {
    case DOWNLOAD:
      line(0, -1, 0, 0.45f), line(-0.55f, -0.1f, 0, 0.45f), line(0.55f, -0.1f, 0, 0.45f), line(-0.75f, 0.95f, 0.75f, 0.95f);
      break;
    case OK: line(-0.65f, 0, -0.15f, 0.5f), line(-0.15f, 0.5f, 0.7f, -0.45f); break;
    case FAIL: line(-0.55f, -0.55f, 0.55f, 0.55f), line(-0.55f, 0.55f, 0.55f, -0.55f); break;
    case BOOT: {
      // a linha de pulso do logo (docs/assets/logo.svg), na escala do anel: pontos em unidades do raio de 176
      static const int16_t P[][2] = {{-106, 0}, {-46, 0}, {-22, -76}, {10, 68}, {34, 0}, {106, 0}};
      const float k = r / 176.f, w = max(1.5f, 13 * k);
      for (int i = 1; i < 6; i++)
        wideLine(cx + P[i - 1][0] * k, cy + P[i - 1][1] * k, cx + P[i][0] * k, cy + P[i][1] * k, w, color::TEXT);
      break;
    }
  }
  if (logo && !strcmp(title, "tokenpulse")) {  // o nome em duas cores, como no logotipo
    const int tw = textWidth("token", TITLE), pw = textWidth("pulse", TITLE), x = cx - (tw + pw) / 2;
    text("token", x, cy + r + s(22), TITLE, color::TEXT, middle_left);
    text("pulse", x + tw, cy + r + s(22), TITLE, color::ACCENT, middle_left);
  } else {
    text(title, cx, cy + r + s(22), TITLE, color::TEXT, middle_center);
  }
  text(status, cx, cy + r + s(44), SMALL, color::MUTED, middle_center);
  });
}

}  // namespace gfx
