# Roadmap

O que entrou primeiro no GeekMagic SmallTV-Ultra e falta levar para as outras placas. Cada item espera a placa
estar conectada para ser testado nela.

| Recurso | Guition JC3248W535 | ESP32-2432S028R | Waveshare ESP32-S3-LCD-1.47B |
|---|---|---|---|
| Alerta de aprovar: pula para as sessões e chama atenção enquanto uma sessão pede permissão | falta (pulsar o backlight) | falta (LED vermelho e backlight) | falta (LED vermelho e backlight) |
| Animações (ponto ao vivo, mola no anel e nas barras, brilho no número) | já tem | já tem | já tem |
| Aviso ao cruzar 70/80/90% da janela de 5 h | já tem | já tem (com LED) | já tem (com LED) |
| Histórico de 14 dias | já tem | já tem | falta no layout compacto |
| % das outras orgs | já tem (com troca pelo toque) | já tem (com troca pelo toque) | falta no layout compacto, só leitura |
| Sessões trabalhando, esperando você e paradas | já tem (por workspace) | já tem (por workspace) | falta no layout compacto |
| Detalhe de cada modelo | já tem | já tem | falta no layout compacto |

A referência de cada item é a implementação do SmallTV em [`firmware/src/smalltv/`](firmware/src/smalltv/) e a
lista de sessões que o daemon já manda pronta (`session_list` em [`host/tokend.py`](host/tokend.py)).
