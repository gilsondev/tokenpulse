#pragma once
// Perfil de uma placa: o hardware que ela tem. A UI liga e desliga recursos por ele, nunca pelo
// nome da placa. PSRAM e tamanho da tela não entram aqui: a UI lê os dois em tempo de execução.
namespace board {

// Energia de uma placa com PMU: bateria presente, USB ligado, carregando, terminando (tensão constante), %
// (-1: sem leitura).
struct PowerState {
  bool battery = false, usb = false, charging = false, finishing = false;
  int pct = -1;
};

struct Profile {
  bool touch;      // toque na tela: engrenagem, cartões de sessão, troca de org
  bool calibrate;  // touch resistivo: calibração de 4 pontos na primeira vez, guardada na NVS
  bool imu;        // acelerômetro: gira a tela sozinha
  bool led;        // LED RGB: alerta de faixa de consumo
  bool button;     // botão físico: avança a tela; segurar volta para a principal
  bool speaker = false;  // alto-falante: som no alerta de aprovação
  bool oled = false;     // tela que marca com imagem parada: parada vira o protetor e a imagem anda
  bool sd = false;       // leitor de cartão SD: histórico guardado além dos 14 dias
  bool battery = false;  // PMU com bateria: o ícone de energia no cabeçalho
  bool dense = false;    // tela pequena com muitos pixels: o cabeçalho sobe um tamanho de fonte
  float pxPerMm = 6.5f;  // densidade da tela: os alvos de toque têm um tamanho mínimo em milímetros, não em pixels
};

}  // namespace board
