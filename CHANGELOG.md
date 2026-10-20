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

- Tema escuro único no portal, na observação, no protetor e nos alertas:
  barra de status de 24 px, cartões arredondados, temperatura em âmbar,
  umidade em azul, ligado em verde. O gato calico, os textos em pt-BR e os
  alvos de toque (faixa de cima, faixa de baixo, **LIMPAR TUDO**, **Porta**)
  continuam os mesmos.
- O globo de internet gira enquanto a sonda passa (8 quadros de longitude,
  500 ms cada). Sem internet ele congela em cinza e recebe um X vermelho
  grosso. A sonda não mudou: HTTP 204 em `cyd_net`, a cada 45 s, com DNS de
  2 s e GET de 2,5 s, nas URLs `connectivitycheck.gstatic.com/generate_204`,
  `www.gstatic.com/generate_204` e `cp.cloudflare.com`, sem redirect. Duas
  falhas seguidas param o globo; um sucesso volta a girar na hora. Sem IP de
  estação o X entra na hora. O chip do HA continua separado. A URL salva em
  `net_url` não pinta o globo.
- Cada quadro ainda é pintado em faixas de 20 px. Uma faixa igual à anterior
  não é reenviada ao LCD, então a animação do gato não redesenha a tela
  inteira. O clock do painel segue em 10 MHz.
- Campainha e botão rápido abrem um visor de snapshot JPEG
  (`GET /api/camera_proxy` no HA, decodificado com o TJpgDec da ROM).
  O botão padrão se chama **Porta** e aponta para a câmera da frente direita.
- O visor reserva um buffer de linha para cada transferência SPI que o
  painel pode deixar na fila (10) e só reutiliza um buffer depois que essa
  fila esvazia. Dois buffers para dez transferências rasgavam o JPEG.
- Se `display_task` não completa uma volta em **45 s**, uma tarefa de
  watchdog reinicia a placa. O visor da câmera renova esse batimento a cada
  quadro; um snapshot que não volta em 45 s também conta como travamento.
- Até cinco linhas. Sintaxe `entidade` ou `entidade=Rótulo`. O padrão de
  fábrica é `switch.carros_switch_1=Garagem 1`,
  `switch.carros_switch_2=Garagem 2`, `switch.casa_aracaju_piscina=Piscina`,
  `switch.casa_ewelink_poco=Poco`.
- As luzes da garagem no padrão de fábrica passam a ser o interruptor Tuya
  **Carros** (`switch.carros_switch_1` e `switch.carros_switch_2`), com os
  rótulos **Garagem 1** e **Garagem 2**. `switch.garagem_switch_1/2` eram um
  interruptor antigo, fora do ar. Vale quando a NVS ainda não tem `c_ents`.
- Relógio com Montserrat semibold e antialiasing (24 h por padrão).
- Gato calico redesenhado em sprites de paleta com RLE (borda já misturada
  ao fundo escuro, sem framebuffer extra). Respira, pisca, balança o rabo,
  mexe a orelha, alonga e boceja, dorme no protetor entre 21 h e 6 h, olha
  para cima quando chega um alerta do HA e ronrona no toque. O tempo de
  cada pose é aleatório. O splash ditherizado da inicialização continua.
- Inversão de cor **desligada** por padrão. A ordem BGR continua disponível e
  só passa a valer depois de reiniciar. Inversão e brilho pedidos por
  `POST /config` ou `POST /display` entram na próxima passagem de
  `display_task` (o HTTP não mexe no painel).
- O globo substitui as quatro barras do canto da barra. Aquelas barras não
  eram RSSI: misturavam o Wi-Fi associado com um GET da URL salva (padrão
  `generate_204`, HTTP 200 ou 204) dentro do poller do HA.

### Ajustes, alertas e OTA

- Ajustes da tela na NVS (`app_cfg`) e na seção **Tela do CYD** do app
  (`GET /config`, `POST /config`).
- Alertas enviados pelo HA: `POST /alert`, `GET /alerts`, selo na home e no
  descanso, lista, botão **LIMPAR TUDO** e toque longo (0,8 s) para limpar.
- `GET /touch` devolve a última leitura do touch e, para diagnosticar
  travamento de longe: `uptime_s`, `disp_age_ms` (ms desde a última volta
  da tela, ou `-1`), `reset` (`esp_reset_reason`) e `heap` / `heap_min`.
- Duas partições OTA (`ota_0` e `ota_1`, 0x1F0000 cada) e rollback. A NVS
  permanece em `0x9000`. Quem já tinha a tabela antiga precisa de um último
  flash por cabo; as trocas seguintes são `POST /ota` com `esp32-ota.bin`.
  A imagem nova se confirma depois de 60 s com Wi-Fi; um reboot antes disso
  volta para o slot anterior.
- O CI (ESP-IDF 5.3.2) publica `firmware-cyd.bin` e `esp32-ota.bin`.
- `POST /reboot?key=<chave>` reinicia a placa cerca de 1,5 s depois.
- `GET /screenshot?key=<chave>` devolve a tela em BMP 24-bit 320×240
  (230 454 bytes, em pedaços, linhas de baixo para cima). O HTTP só
  enfileira; `display_task` lê uma linha por vez para um buffer de 2,5 KB.
  Chave ausente ou errada responde `403`. Sem a primeira faixa em ~3 s,
  responde `503`. O watchdog de 45 s segue alimentado durante a captura.
  No HA: `shell_command` com `curl` para `/config/www/cyd/screen.bmp`,
  botão **CYD: capturar tela** e câmera `local_file`.
- A leitura da GRAM do screenshot cabe no FIFO de 64 bytes (o LCD continua
  sem DMA: DMA no SPI2 reinicia o CYD com o Wi-Fi). Cada linha sai em
  pedaços de 21 pixels com `RAMRD` (`0x2E`) e `RAMRD continue` (`0x3E`):
  um byte dummy e RGB666 em 3 bytes/px. O clock do painel desce para
  10 MHz, o teto usual da leitura do ILI9341. Falha de leitura vira `500`
  `read_failed` (com o `esp_err` no log), em vez de `503` `timeout`.
- `cyd_cfg_load()` monta uma cópia e troca `s_cfg` com um único `memcpy`.
