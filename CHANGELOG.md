# Changelog

## Não lançado

Firmware do Cheap Yellow Display 2.8" (ESP-IDF 5.3.2), em cima do portal Wi-Fi
e da observação do Home Assistant. Testado na placa pelo autor da série.

### Home Assistant

- O token longo cabe em **319** caracteres (antes era cortado em 63).
- O chat usa o Assist de verdade: `POST /api/conversation/process` (pt-BR),
  guardando `conversation_id` entre frases. O backend simulado
  (`CONFIG_APP_BOT_BACKEND_MOCK`) fica opcional e desligado por padrão.
- A leitura do HA passou de `GET /api/states` (estado inteiro) para um
  `POST /api/template` pequeno: temperatura, umidade, as linhas configuradas,
  atualizações e, se ligado, a ração do dia. Falha de rede ou token aparece
  como `offline` / `token ruim` / `sem token` / `sem hub`, sem girar para sempre.
- `GET /devices` e `GET /weather` usam esse resumo quando o backend é o live.

### Painel

- Campainha e botão rápido abrem um visor de snapshot JPEG
  (`GET /api/camera_proxy` no HA, decodificado com o TJpgDec da ROM).
  O botão padrão se chama **Porta** e aponta para a câmera da frente direita.
- Até cinco linhas. Sintaxe `entidade` ou `entidade=Rótulo`. O padrão de
  fábrica é `switch.garagem_switch_1=Garagem 1`,
  `switch.garagem_switch_2=Garagem 2`, `switch.casa_aracaju_piscina=Piscina`,
  `switch.casa_ewelink_poco=Poco`.
- Relógio com Montserrat semibold e antialiasing (24 h por padrão).
- Gato calico com comportamentos (parado, dorme, alonga, pata, olha em volta)
  e splash ditherizado na inicialização.
- Inversão de cor **desligada** por padrão. A ordem BGR continua disponível e
  só passa a valer depois de reiniciar. Inversão e brilho pedidos por
  `POST /config` ou `POST /display` entram na próxima passagem de
  `display_task` (o HTTP não mexe no painel).
- Ícone de internet na tela de descanso (URL configurável; espera HTTP 200 ou 204).

### Ajustes, alertas e OTA

- Ajustes da tela na NVS (`app_cfg`) e na seção **Tela do CYD** do app
  (`GET /config`, `POST /config`).
- Alertas enviados pelo HA: `POST /alert`, `GET /alerts`, selo na home e no
  descanso, lista, botão **LIMPAR TUDO** e toque longo (0,8 s) para limpar.
- `GET /touch` devolve a última leitura do touch para depuração.
- Duas partições OTA (`ota_0` e `ota_1`, 0x1F0000 cada) e rollback. A NVS
  permanece em `0x9000`. Quem já tinha a tabela antiga precisa de um último
  flash por cabo; as trocas seguintes são `POST /ota` com `esp32-ota.bin`.
  A imagem nova se confirma depois de 60 s com Wi-Fi; um reboot antes disso
  volta para o slot anterior.
- O CI (ESP-IDF 5.3.2) publica `firmware-cyd.bin` e `esp32-ota.bin`.
- `POST /reboot?key=<chave>` reinicia a placa cerca de 1,5 s depois.
- `cyd_cfg_load()` monta uma cópia e troca `s_cfg` com um único `memcpy`.
