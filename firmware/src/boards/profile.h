#pragma once
// Perfil de uma placa: o hardware que ela tem. A UI liga e desliga recursos por ele, nunca pelo
// nome da placa. PSRAM e tamanho da tela não entram aqui: a UI lê os dois em tempo de execução.
namespace board {

struct Profile {
  bool touch;      // toque na tela: engrenagem, cartões de sessão, troca de org
  bool calibrate;  // touch resistivo: calibração de 4 pontos na primeira vez, guardada na NVS
  bool imu;        // acelerômetro: gira a tela sozinha
  bool led;        // LED RGB: alerta de faixa de consumo
  bool button;     // botão físico: avança a tela; segurar volta para a principal
  bool speaker = false;  // alto-falante: som no alerta de aprovação
  bool oled = false;     // tela que marca com imagem parada: parada vira o protetor e a imagem anda
  bool dense = false;    // tela pequena com muitos pixels: o cabeçalho sobe um tamanho de fonte
};

}  // namespace board
