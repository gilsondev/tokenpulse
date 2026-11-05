// Stand inclinado sem parafuso para a Waveshare ESP32-S3-LCD-1.47B (paisagem, USB-C à direita).
// Inspirado no "Waveshare ESP32-C6 1.47 — Landscape stand" (https://makerworld.com/en/models/3408098):
// 65° em relação à mesa, cabo saindo pela direita, BOOT e RST livres.
//
// Encaixe: os 4 espaçadores de latão das costas da placa entram em 4 pinos impressos (pressão).
// Uma prateleira sob a borda de baixo segura o peso. Imprima primeiro part="teste" para acertar
// pino_d: o pino deve entrar firme com o dedo, sem precisar de força.
//
// Medidas da placa: https://www.waveshare.com/img/devkit/ESP32-S3-LCD-1.47B/ESP32-S3-LCD-1.47B-details-size.jpg
//
// Renderizar:
//   openscad -D 'part="stand"' -o stand.stl stand-waveshare-s3-lcd-147b.scad
//   openscad -D 'part="teste"' -o teste.stl stand-waveshare-s3-lcd-147b.scad

part = "stand";  // "stand" ou "teste"

// --- placa (vista de frente, USB-C à direita, origem no canto inferior esquerdo da PCB) ---
pcb_w = 36.37;
pcb_h = 20.32;
pcb_t = 1.0;           // espessura da PCB; meça se puder (1,0 a 1,6)
espacador_h = 2.17;    // quanto os espaçadores de latão saem das costas da PCB (medido com paquímetro)
furos = [              // centros dos furos M2
  [1.97, 3.52], [1.97, pcb_h - 3.52],
  [pcb_w - 2.40, 2.00], [pcb_w - 2.40, pcb_h - 2.00],
];
zona_botoes = 11.31;   // BOOT e RST ficam nos últimos 11,31 mm do lado do USB-C

// --- encaixe (calibre com a peça de teste) ---
pino_d = 1.85;         // M2: entra na rosca por pressão. Solto: aumente 0,05. Não entra: diminua 0,05
pino_len = 3.0;
ressalto_d = 3.6;      // apoio onde a ponta do espaçador encosta
ressalto_h = 1.5;      // folga extra para os componentes das costas

// --- stand ---
inclinacao = 65;       // graus em relação à mesa
placa_t = 2.4;         // espessura da chapa de trás
margem = 2.0;
base_w = 62;
base_d = 46;
base_t = 3.0;

$fn = 40;

module pino() {  // ressalto onde a ponta do espaçador encosta + pino que entra na rosca
  translate([0, 0, -0.2]) cylinder(d = ressalto_d, h = ressalto_h + 0.2);  // afunda na chapa para fundir depois da rotação
  translate([0, 0, ressalto_h]) cylinder(d1 = pino_d, d2 = pino_d - 0.3, h = pino_len);
}

prateleira_alt = 2.4;                                      // espessura da prateleira
prateleira_prof = ressalto_h + espacador_h + pcb_t + 1.5;  // vai até 1,5 mm à frente da PCB
prateleira_x = pcb_w - zona_botoes - 1;                    // para antes do RST

// Peças do painel no plano XY; a frente da placa aponta para +Z.
module chapa() {
  translate([-margem, -prateleira_alt, -placa_t])
    cube([pcb_w + 2 * margem, pcb_h + margem + prateleira_alt, placa_t]);
}
module prateleira() {
  translate([0, -prateleira_alt, -placa_t]) cube([prateleira_x, prateleira_alt, placa_t + prateleira_prof]);
}
module pinos() { for (f = furos) translate([f[0], f[1], 0]) pino(); }

// Coloca uma peça do painel inclinada, com o ponto mais baixo encostado na base.
module inclinado() {
  sobe = prateleira_alt * sin(inclinacao) + placa_t * cos(inclinacao);
  translate([0, 0, base_t + sobe]) rotate([inclinacao, 0, 0]) children();
}

// Preenche da peça inclinada até a base: tudo vira rampa e imprime sem suporte.
module ate_a_base() {
  hull() {
    inclinado() children();
    translate([0, 0, base_t - 0.01]) linear_extrude(0.01) projection() inclinado() children();
  }
}

module stand() {
  base_x0 = -(base_w - pcb_w) / 2;
  translate([base_x0, -14, 0]) cube([base_w, base_d, base_t]);
  ate_a_base() chapa();       // só atrás da chapa: a frente fica livre para a placa
  ate_a_base() prateleira();  // a prateleira tem a própria rampa embaixo
  inclinado() pinos();
  // gancho do cabo atrás, à direita: o cabo sai do USB-C, desce e passa por aqui
  translate([pcb_w + margem + 2, 18, base_t]) difference() {
    cube([8, 10, 8]);
    translate([2, -1, 2]) cube([4, 12, 7]);
  }
}

module teste() {
  // só a chapa com os pinos e a prateleira, impressa deitada: ~5 min
  chapa();
  prateleira();
  pinos();
}

if (part == "teste") teste();
else stand();
