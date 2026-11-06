#pragma once
#include <Arduino.h>

#include <functional>

namespace screen {

void begin();
void update(bool newFrame);  // redesenha quando chega frame e uma vez por segundo
void otaProgress(size_t done, size_t total);  // chamado pelo Updater a cada bloco do OTA pelo /update
// Desenha uma tela (main, sessions, models, model0..2, projects, waiting) e entrega as faixas em RGB565 de
// byte alto primeiro, de cima para baixo, sem mexer no visor. false se a tela não existe agora.
bool shot(const String& name, const std::function<void(const uint8_t*, size_t)>& write);

}  // namespace screen
