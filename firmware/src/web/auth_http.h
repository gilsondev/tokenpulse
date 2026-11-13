#pragma once
// As rotas do pareamento e a checagem do token, para quem tem o servidor web (webui.cpp, smalltv/net.cpp).
#include "auth.h"

namespace webauth {

template <class Server>
bool authed(Server& s, const char* token) {
  if (!token || !*token) return false;
  return s.header("Cookie").indexOf(String("tp=") + token) >= 0 || s.header("X-Token") == token;
}

// POST /api/pair/start mostra um código novo na tela; POST /api/pair?code=NNNN devolve o cookie. Cinco erros
// apagam o código: é preciso pedir outro, olhando para a placa de novo.
template <class Server>
void routes(Server& s, const char* token) {
  s.on("/api/pair/start", HTTP_POST, [&s] {
    code = 1000 + TP_RANDOM() % 9000, codeMs = millis(), misses = 0;
    s.send(204);
  });
  s.on("/api/pair", HTTP_POST, [&s, token] {
    if (!codeActive() || s.arg("code").toInt() != code) {
      if (codeActive() && ++misses >= 5) code = 0;
      return s.send(403, "text/plain", "Código errado ou vencido: peça outro.");
    }
    code = 0;
    s.sendHeader("Set-Cookie", String("tp=") + token + "; Max-Age=31536000; Path=/; SameSite=Strict");
    s.send(204);
  });
}

}  // namespace webauth
