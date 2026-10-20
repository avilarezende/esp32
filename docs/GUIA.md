# Guia do firmware ESP32 + CYD

O que o firmware faz, como as telas se comportam e como ligar o painel amarelo
(Cheap Yellow Display 2.8") a um Home Assistant na rede local.

## O que é

Firmware para **ESP32** (ESP-IDF 5.3.2) que:

1. Entra na rede Wi-Fi salva ou sobe o portal `ESP32-Setup` no primeiro uso.
2. Serve um app web no celular (chat, Casa, Ajustes, **Tela do CYD**, OTA).
3. No **CYD 2.8"** (ESP32-2432S028, ILI9341 + XPT2046) desenha relógio, clima,
   dispositivos, câmera e alertas.

Placa alvo do painel: **só a amarela de 2.8"**. A de 3.5" usa outro controlador
e não acende com esta imagem.

## Telas do painel

### Aparência

O painel usa um tema escuro só, nas quatro páginas (portal, observação,
protetor e alertas):

- Fundo azul-noite, barra de status de 24 px e cartões arredondados na mesma
  margem de 8 px.
- A barra traz, da esquerda para a direita, o título ou o relógio, o selo de
  alertas, o estado do HA (na observação) e o ícone de internet.
- Temperatura em âmbar e umidade em azul. Ligado é verde, desligado é o texto
  apagado, qualquer outro estado é azul.
- O gato calico continua na observação e no protetor. O desenho é um
  sprite em paleta (RLE), com borda antialiasada no fundo escuro: pelo
  branco, mancha laranja e mancha preta, orelha esquerda preta, orelha
  direita laranja, bigodes e olhos com brilho. Não entra no portal nem na
  lista de alertas.
- O toque não mudou de lugar: a faixa de cima (à esquerda do botão **Porta**)
  e a faixa de baixo abrem os alertas; **LIMPAR TUDO** segue em `y >= 180`;
  o botão **Porta** segue no canto superior direito.
- Cada quadro é pintado em faixas de 20 px. Uma faixa igual à do quadro
  anterior não vai de novo ao painel, então o gato não redesenha a tela
  inteira. O clock do LCD continua em 10 MHz.

### Splash

Na subida o painel mostra um gato calico ditherizado (vários quadros) e em
seguida a tela de observação ou o portal.

### Portal (sem Wi-Fi de casa)

Enquanto a placa não tem IP de estação, o painel mostra um cartão com o jeito
de conectar o celular. A barra de status diz **Portal** e o ícone de internet
fica com o corte de “sem rede”:

![Portal no painel](images/cyd-panel-portal.png)

1. Entre no Wi-Fi `ESP32-Setup`, senha `esp32setup`.
2. Abra `http://192.168.4.1`.
3. Salve a rede de casa e, no onboarding, o Home Assistant.

### Observação (conectado ao HA)

Depois que a estação ganha IP, o firmware manda um `POST /api/template` pequeno
ao HA (não baixa mais o `/api/states` inteiro) e monta a tela:

![Tela de observação](images/cyd-home-ha.png)

- Hora (Montserrat pequeno, 24 h por padrão) na barra, e o status do HA num
  chip à direita (`HA ok` em verde, `aguardando` apagado, o resto em âmbar).
- Lugar (Casa) abaixo da barra. Temperatura e umidade ficam em dois cartões
  quando o HA tem `weather` ou sensores; sem os dois, o cartão diz
  `clima indisponivel`.
- Até **cinco** linhas. Cada item da lista pode ser só a entidade
  (`switch.casa_aracaju_piscina`) ou `entidade=Rótulo`
  (`switch.carros_switch_1=Garagem 1`). O rótulo fica com no máximo 17
  caracteres.
- Padrão de fábrica: `switch.carros_switch_1=Garagem 1`,
  `switch.carros_switch_2=Garagem 2`, `switch.casa_aracaju_piscina=Piscina`,
  `switch.casa_ewelink_poco=Poco`. Vale quando a NVS ainda não tem `c_ents`.
  Se a opção de ração estiver ligada, a última linha mostra as porções de hoje
  (Spock, Kirk, Gatas) lidas de
  `sensor.alimentacao_spock_hoje`, `sensor.alimentacao_kirk_hoje` e
  `sensor.alimentacao_gatas_hoje`.
- As linhas ficam num cartão, com um ponto de estado e um fio entre elas.
  Sem linhas, a mensagem de erro (ou “nenhuma luz…”) fica no centro e o gato
  maior embaixo.
- Botão **Porta** (canto superior direito, o mesmo retângulo de toque): abre
  a câmera rápida (padrão `camera.frigate_frente_direita`). O rótulo e o
  ícone (portão ou câmera) mudam em Ajustes.
- Selo de alertas na barra de status, quando há alertas. A faixa do alerta
  mais recente continua embaixo.

Estados que aparecem no lugar de um spinner infinito:

| Status | Significado |
|--------|-------------|
| `HA ok` | O template respondeu 200 |
| `aguardando` | Ainda não houve resposta nesta subida |
| `sem hub` | Nenhum endereço de HA salvo |
| `sem token` | Falta o token longo |
| `token ruim` | O HA respondeu 401 |
| `offline` | Sem resposta, ou outro código HTTP |
| `hub nao HA` | O hub salvo não é do tipo Home Assistant |
| `sem mem` | Sem heap para o pedido |

### Câmera

O visor pede ao HA `GET /api/camera_proxy/<entidade>?width=320&height=240` e
desenha o JPEG com o decodificador da ROM (TJpgDec). Imagens mais largas que
640 px são reduzidas.

Duas formas de abrir:

- **Campainha.** O HA chama `POST /camera` (pacote `cyd_camera`). A câmera
  padrão de fábrica é `camera.frigate_frente_esquerda`. A duração padrão é 20 s
  e o quadro atualiza a cada 1 s (os dois são ajustáveis).
- **Botão Porta.** Um toque no canto superior direito abre a câmera rápida
  pelo mesmo tempo.

O painel aceita até **10** transferências SPI enfileiradas, e o desenho de
um bloco volta antes da transferência terminar. O visor guarda um buffer de
linha por vaga dessa fila. Quando as dez estão ocupadas, espera a fila
esvaziar antes de reutilizar um buffer — senão o JPEG sai rasgado.

Cada quadro renova o batimento da tarefa da tela. Se o download do snapshot
ou o decodificador não voltam em **45 s**, o watchdog reinicia a placa
(veja [Travamento da tela](#travamento-da-tela)).

### Protetor de tela

Após o tempo de descanso (padrão **30 s**) sem toque, o relógio grande toma a
tela. A data fica na barra, junto com o ícone de internet; lugar, temperatura
e umidade ficam num cartão; o gato calico fica no centro:

![Protetor](images/cyd-saver-ha.png)

O ícone consulta a URL salva (padrão
`http://connectivitycheck.gstatic.com/generate_204`). Barras verdes: HTTP 200 ou
204. Barras com um corte vermelho: sem Wi-Fi ou Wi-Fi sem essa resposta. Cinza:
ainda não houve teste. Na observação e nos alertas o mesmo ícone fica na barra.

Um toque volta à observação. Após **40 minutos** sem uso a backlight apaga; um
toque acorda o painel. O gato respira, pisca devagar, mexe o rabo e, de vez
em quando, a orelha. De tempos em tempos alonga e boceja, ou olha para cima.
Entre 21 h e 6 h, no protetor, ele dorme (com z). Um alerta novo do HA faz
ele erguer as orelhas; um toque faz ele ronronar. O intervalo de cada pose
é sorteado. Dá para desligar a animação em Ajustes.

### Alertas

O HA empurra alertas com `POST /alert`. O painel guarda até 8, mostra um selo
(`! N`) na barra da observação e do descanso e uma faixa arredondada com o
mais recente. A lista usa um cartão por alerta, com uma faixa colorida à
esquerda, e o botão **LIMPAR TUDO** ocupa a faixa de baixo.

- Toque no selo, na faixa de cima (à esquerda do botão Porta) ou na faixa de
  baixo abre a lista.
- Na lista, um toque fora do botão só fecha (“visto”) e **não** apaga.
- **LIMPAR TUDO** (faixa vermelha embaixo) apaga todos.
- Toque longo de **0,8 s** em qualquer lugar, enquanto existir alerta, também
  apaga todos.
- `duration` em segundos remove o alerta sozinho. `0` deixa até alguém limpar.
- `level`: `info` (azul), `warn` (laranja) ou `crit` (vermelho).

Acentos saem dobrados para ASCII na fonte do painel (`ã` vira `a`).

### Cores

A inversão do painel vem **desligada** (`inv=0`), que é o certo para esta
placa. A ordem **BGR** vem ligada. `POST /config` e `POST /display` só gravam
o ajuste e levantam um pedido: inversão e brilho entram na **próxima passagem**
da tarefa da tela (`display_task`). O servidor HTTP não chama
`esp_lcd_panel_invert_color()` nem o controle da backlight — fazer isso no
meio de um quadro SPI travava o painel e o HTTP depois de `POST /config`.
BGR continua valendo só depois de reiniciar. `POST /display` também aceita
`test=1` (barras de cor por 15 s).

## App web no celular

O mesmo aparelho serve a interface no navegador, na porta **80**:

| Chat | Casa |
|------|------|
| ![Chat](images/cyd-chat.png) | ![Casa](images/cyd-casa.png) |

- **Chat** manda o texto ao Assist (`/api/conversation/process`, idioma pt-BR)
  e mantém a conversa entre mensagens.
- **Casa** lista as mesmas linhas do painel.
- **Ajustes → Tela do CYD** grava em `GET /config` e `POST /config`: entidades,
  ração, câmeras, rótulo e ícone do botão, duração da campainha, intervalo do
  snapshot, brilho (5–100%), inverter cores, BGR, segundos até o descanso,
  gato, relógio 24 h, alertas e URL de teste de internet.
- **Atualizar firmware** envia `esp32-ota.bin` (ou uma URL) com a chave do
  aparelho. Ver [INSTALACAO.md](INSTALACAO.md).

## Home Assistant

### Token

No HA: perfil do usuário → **Long-Lived Access Tokens** → **Create Token**.
Cole o valor no campo **senha** do onboarding ou de Ajustes do CYD. Não use a
senha de login. O firmware guarda até **319** caracteres.

### Endereço (porta 80)

No onboarding, o exemplo é a **porta 80** do HA:

| Campo | Valor |
|-------|--------|
| Tipo | Home Assistant |
| Endereço | `http://192.168.0.10` |
| Senha | token longo |

`http://192.168.0.10` chama `http://192.168.0.10/api/template` e
`http://192.168.0.10/api/conversation/process`. Se o seu HA ainda escuta em
8123, use `http://192.168.0.10:8123`. O exemplo antigo
`homeassistant.local:8123` não é mais o padrão desta documentação: mDNS e a
porta 8123 falham em várias redes em que um reverse proxy na porta 80 já
funciona.

O poller roda a cada ~30 s enquanto houver Wi-Fi.

### Chave do aparelho

`/camera`, `/alert`, `/display`, `/ota`, `/reboot` e `/screenshot` exigem uma
chave: os **16 primeiros caracteres hexadecimais** do SHA-256 do token longo
(o mesmo salvo no CYD).

```bash
printf '%s' 'SEU_TOKEN' | sha256sum | cut -c1-16
```

Não commite o token nem a chave. No app, o campo “Chave do dispositivo” é só
para o envio de OTA feito dali; o HA guarda a chave em `secrets.yaml`.

### Pacote `cyd_camera` / `cyd_alert`

O arquivo [homeassistant/cyd.yaml](homeassistant/cyd.yaml) define:

- `rest_command.cyd_camera` — `POST http://IP_DO_CYD/camera` (porta **80** do
  painel, não a porta do HA) com `entity` e `seconds`.
- `rest_command.cyd_alert` — `POST http://IP_DO_CYD/alert` com `id`, `title`,
  `message`, `level` e `duration`.
- `shell_command.cyd_screenshot` — `curl` para `GET /screenshot`, gravando
  `/config/www/cyd/screen.bmp`.
- O botão **CYD: capturar tela** e a câmera `local_file` **CYD tela**.
- Uma automação de exemplo: campainha liga → abre a câmera da frente esquerda.

`secrets.yaml`:

```yaml
cyd_camera_url: "http://192.168.0.50/camera?key=SUA_CHAVE_16"
cyd_alert_url: "http://192.168.0.50/alert?key=SUA_CHAVE_16"
cyd_screenshot_cmd: 'curl -fsS -o /config/www/cyd/screen.bmp "http://192.168.0.50/screenshot?key=SUA_CHAVE_16"'
```

Troque `192.168.0.50` pelo IP do CYD e os `entity_id` do pacote pelos seus.
Para dispensar um alerta pelo HA: `POST /alert` com `dismiss=<id>` ou
`clear=1`.

`GET /alerts` devolve a lista em JSON. `GET /touch` devolve a última leitura
do XPT2046 e o diagnóstico da tarefa da tela
([Travamento da tela](#travamento-da-tela)).

### Reinício remoto

`POST /reboot?key=SUA_CHAVE_16` responde `{"ok":true,"reboot":true}` e
reinicia a placa cerca de **1,5 s** depois (o mesmo atraso do OTA, para a
resposta HTTP sair antes). Sem a chave a resposta é `403` com
`{"ok":false,"error":"key"}`. Serve para recuperar o painel sem desligar da
tomada.

```bash
curl -X POST "http://IP_DO_CYD/reboot?key=SUA_CHAVE_16"
```

### Captura de tela

`GET /screenshot?key=SUA_CHAVE_16` devolve a tela em BMP de 24 bits, 320×240
(230 454 bytes: cabeçalho de 54 e as linhas de baixo para cima). A resposta
sai em pedaços; o firmware não monta um quadro inteiro na RAM.

A chave é a mesma de `POST /reboot`. Sem ela, ou com o valor errado, a
resposta é `403` com `{"ok":false,"error":"key"}`.

O servidor HTTP só enfileira o pedido. Quem lê o painel é a `display_task`
— o mesmo desenho de `POST /config`, em que o HTTP no meio de um quadro SPI
travava a placa. A tarefa lê uma linha por vez: se houver um sprite ou
framebuffer, copia dali; senão, lê a GRAM do ILI9341 (MISO no GPIO 12).

O barramento do LCD fica sem DMA (`SPI_DMA_DISABLED`). DMA no SPI2 briga
com o Wi-Fi e reinicia o CYD, e sem DMA cada transferência cabe em **64
bytes**. A leitura da GRAM começa com `RAMRD` (`0x2E`) e segue com
`RAMRD continue` (`0x3E`). Os dois comandos devolvem um byte dummy e depois
RGB666 (3 bytes por pixel, 6 bits no topo), então cada pedaço tem 21 pixels
(`1 + 21×3 = 64`; os 5 pixels finais da linha de 320 vão num último
pedaço). O clock do painel é **10 MHz** para escrita e leitura: o
dispositivo SPI tem um clock só, e o readback do ILI9341 não é confiável
acima disso.

Cada faixa de 4 linhas (2,5 KB de RGB565) volta por uma fila. O handler
converte para BMP e envia. Se a primeira faixa não chega em cerca de
**3 s**, a resposta é `503` com `{"ok":false,"error":"timeout"}` (ou
`"busy"` se já existe uma captura). Se a leitura do painel falha, a
resposta é `500` com `{"ok":false,"error":"read_failed"}` e o log da
`display_task` traz o `esp_err`. Enquanto a leitura corre, o batimento do
watchdog de 45 s continua sendo renovado.

```bash
curl -fsS -o tela.bmp "http://IP_DO_CYD/screenshot?key=SUA_CHAVE_16"
```

No Home Assistant o pacote faz três coisas:

1. `shell_command` com `curl`, gravando em `/config/www/cyd/screen.bmp`
   (a pasta `www` do HA é servida em `/local/`).
2. Um botão **CYD: capturar tela**, que chama esse comando.
3. Uma câmera `local_file` (**CYD tela**) apontando para o mesmo arquivo.

Crie a pasta uma vez no host do HA (`mkdir -p /config/www/cyd`). A câmera
`local_file` só fica disponível depois que `screen.bmp` existe; o primeiro
toque no botão cria o arquivo. A URL da imagem no painel do HA é
`/local/cyd/screen.bmp`.

### Travamento da tela

Uma tarefa (`cyd_wdt`) olha o batimento de `display_task`. Se a tarefa da
tela não completa uma volta em **45 s**, a placa reinicia sozinha
(`esp_restart`). Isso vale dentro do visor da câmera: cada quadro renova o
batimento, e um `GET` do snapshot que passa de 45 s também conta.

`GET /touch` não exige a chave. Serve para ver o travamento sem o cabo:

| Campo | Significado |
|-------|-------------|
| `raw_x`, `raw_y`, `x`, `y`, `count` | Última leitura do XPT2046 |
| `uptime_s` | Segundos desde o boot |
| `disp_age_ms` | Milissegundos desde a última volta de `display_task` (`-1` antes da primeira) |
| `reset` | `esp_reset_reason()`: 1 power-on, 3 `esp_restart` (inclui este watchdog), 4 panic, 5 interrupt WDT, 6 task WDT, 9 brownout |
| `heap`, `heap_min` | Heap livre agora e o mínimo desde o boot |

```bash
curl -s "http://IP_DO_CYD/touch"
```

## Tempos

| Evento | Tempo |
|--------|-------|
| Protetor (relógio grande) | 30 s sem toque (ajustável, 10–3600 s) |
| Tela off (backlight) | 40 min sem toque |
| Atualização do HA | ~30 s |
| Snapshot da câmera | 1 s (ajustável, 300–10000 ms) |
| Visor da campainha / Porta | 20 s (ajustável, 3–120 s) |
| Toque longo limpa alertas | 0,8 s |
| Confirmação do OTA (rollback) | 60 s com Wi-Fi |
| Reinício de `POST /reboot` e do OTA | ~1,5 s depois da resposta |
| `GET /screenshot` sem a primeira faixa | ~3 s, depois `503` |
| `GET /screenshot` com falha de leitura | resposta imediata `500` `read_failed` |
| Watchdog da tarefa da tela | 45 s sem uma volta de `display_task` (câmera e captura incluídas) |

## Build

Veja [INSTALACAO.md](INSTALACAO.md). Resumo:

```bash
. "$HOME/esp/esp-idf/export.sh"
idf.py -B build_cyd -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.cyd" build
idf.py -B build_cyd -p PORTA flash monitor
```

## Pacote pronto

A pasta [`release/cyd/`](../release/cyd/) traz a imagem única
`firmware-cyd.bin` (offset `0x0`), o `esp32-ota.bin` para o `POST /ota`, os
binários separados e os scripts `flash.bat` / `flash.sh`.
