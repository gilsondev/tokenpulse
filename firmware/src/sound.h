#pragma once
// Sons dos avisos, dos pacotes Interface Sounds e Digital Audio da Kenney (CC0): data/sounds/LICENSE-kenney*.txt.
// PCM 24 kHz mono de 16 bits, embutido no firmware das placas com alto-falante (board_build.embed_files).
namespace sound {

constexpr const char* NAMES[] = {"Pergunta 1", "Pergunta 2", "Pergunta 3", "Pergunta 4",
                                  "Confirmação 1", "Confirmação 2", "Vidro", "Gongo", "Recarga",
                                  "Três tons", "Pep"};
constexpr int COUNT = sizeof NAMES / sizeof *NAMES;

}  // namespace sound
