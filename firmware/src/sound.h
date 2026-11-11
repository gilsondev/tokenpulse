#pragma once
// Sons do alerta de aprovação, do pacote Interface Sounds da Kenney (CC0): data/sounds/LICENSE-kenney.txt.
// PCM 16 kHz mono de 16 bits, embutido no firmware das placas com alto-falante (board_build.embed_files).
namespace sound {

constexpr const char* NAMES[] = {"Pergunta 1", "Pergunta 2", "Pergunta 3", "Pergunta 4",
                                  "Confirmação 1", "Confirmação 2", "Vidro", "Gongo"};
constexpr int COUNT = sizeof NAMES / sizeof *NAMES;

}  // namespace sound
