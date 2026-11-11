#include "sd.h"

#ifdef TOKENPULSE_SD
#include <FS.h>
#include <SD_MMC.h>

#include <map>

#include "app.h"
#include "boards/board.h"

namespace sd {
namespace {
constexpr const char* DIR = "/tokenpulse";
constexpr const char* FILE_NOW = "/tokenpulse/historico.csv";
constexpr const char* FILE_NEW = "/tokenpulse/historico.new";
constexpr const char* FILE_OLD = "/tokenpulse/historico.bak";
constexpr uint32_t SAVE_MS = 10 * 60 * 1000;

State st = NONE;
std::map<String, String> rows;  // "AAAA-MM-DD" -> "tokens em milhões;custo"
uint32_t savedMs = 0;
String lastToday;

bool mount(bool formatIfNeeded) {
  pinMode(board::SD_PINS[3], OUTPUT);  // D3/CS em alto: o cartão fica no modo SD, não no SPI
  digitalWrite(board::SD_PINS[3], HIGH);
  SD_MMC.setPins(board::SD_PINS[0], board::SD_PINS[1], board::SD_PINS[2]);
  return SD_MMC.begin("/sdcard", true, formatIfNeeded);
}

void load() {
  rows.clear();
  // o arquivo atual; sem ele (a queda pegou a troca no meio), a cópia anterior
  File f = SD_MMC.open(FILE_NOW);
  if (!f) f = SD_MMC.open(FILE_OLD);
  while (f && f.available()) {
    String line = f.readStringUntil('\n');
    const int sep = line.indexOf(';');
    if (line.length() > 11 && sep == 10) rows[line.substring(0, 10)] = line.substring(11);
  }
  if (f) f.close();
}

bool write() {
  File f = SD_MMC.open(FILE_NEW, FILE_WRITE);
  if (!f) return false;
  for (auto& [day, v] : rows) f.printf("%s;%s\n", day.c_str(), v.c_str());
  f.flush();
  const bool ok = f.size() > 0;
  f.close();
  if (!ok) return false;
  SD_MMC.remove(FILE_OLD);
  SD_MMC.rename(FILE_NOW, FILE_OLD);
  return SD_MMC.rename(FILE_NEW, FILE_NOW);
}
}  // namespace

void begin() {
  if (!mount(false)) {  // sem o pino de cartão presente, ausente e sem formato parecem iguais: formatar() decide
    SD_MMC.end();
    st = UNREADABLE;
    return;
  }
  SD_MMC.mkdir(DIR);
  load();
  st = READY;
}

State state() { return st; }
uint32_t freeMB() { return st == READY ? (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024 * 1024) : 0; }
int days() { return rows.size(); }

void saveHistory(JsonArrayConst h) {
  const uint32_t now = app::nowLocal();
  if (st != READY || !now || h.isNull() || !h.size()) return;
  const int n = h.size();
  char today[11];
  const time_t t0 = now;
  struct tm tm;
  gmtime_r(&t0, &tm);
  strftime(today, sizeof today, "%Y-%m-%d", &tm);
  // grava ao virar o dia, ou a cada 10 min para o total de hoje
  if (lastToday == today && savedMs && millis() - savedMs < SAVE_MS) return;
  for (int i = 0; i < n; i++) {
    const time_t t = now - (time_t)(n - 1 - i) * 86400;  // o último da lista é hoje
    gmtime_r(&t, &tm);
    if (tm.tm_mday != (int)(h[i][1] | 0)) continue;  // a data não bate com a do Mac: não arrisca
    char day[11], v[24];
    strftime(day, sizeof day, "%Y-%m-%d", &tm);
    snprintf(v, sizeof v, "%.1f;%d", (float)(h[i][2] | 0.0f), (int)(h[i][3] | 0));
    rows[day] = v;
  }
  if (write()) savedMs = millis() | 1, lastToday = today;
  else st = UNREADABLE;  // o cartão saiu ou falhou: para de tentar até reiniciar
}

bool format() {
  if (st == READY) return true;
  SD_MMC.end();
  if (!mount(true)) {  // ainda falhou: não há cartão
    SD_MMC.end();
    st = NONE;
    return false;
  }
  SD_MMC.mkdir(DIR);
  load();
  st = READY;
  return true;
}

}  // namespace sd

#else  // placa sem cartão

namespace sd {
void begin() {}
State state() { return NONE; }
uint32_t freeMB() { return 0; }
int days() { return 0; }
void saveHistory(JsonArrayConst) {}
bool format() { return false; }
}  // namespace sd

#endif
