# esp32

Firmware ESP-IDF para o **Cheap Yellow Display 2.8"** (ESP32-2432S028) e um
portal Wi-Fi. No boot o aparelho entra na rede salva na NVS; sem credenciais
(ou se a conexão falha), sobe o portal `ESP32-Setup`. Com o Home Assistant na
LAN, o painel mostra relógio, clima, dispositivos, câmera e alertas.

O mesmo repositório ainda traz um [firmware de referência PlatformIO](#firmware-de-referência-platformio)
(Wi-Fi + MQTT + OTA Arduino), independente do CYD.

### Documentação (pt-BR)

| Documento | Conteúdo |
|-----------|----------|
| [docs/GUIA.md](docs/GUIA.md) | Telas, chat, câmera, alertas, ajustes e o lado Home Assistant |
| [docs/INSTALACAO.md](docs/INSTALACAO.md) | Gravação, **último flash por cabo** e OTA |
| [docs/homeassistant/cyd.yaml](docs/homeassistant/cyd.yaml) | Pacote `rest_command`, captura de tela e câmera `local_file` |
| [CHANGELOG.md](CHANGELOG.md) | O que entrou neste firmware |
| [release/cyd/](release/cyd/) | Binários prontos + `flash.bat` / `flash.sh` |

Gravação que **preserva** Wi-Fi, token e ajustes (Windows, PowerShell do ESP-IDF):

```bat
cd release\cyd
flash.bat COM3
```

## O que o CYD faz

- Portal SoftAP `ESP32-Setup` / `esp32setup` e app web no celular.
- Chat real pelo Assist do HA (`POST /api/conversation/process`). O mock local
  fica atrás de `CONFIG_APP_BOT_BACKEND_MOCK` (desligado por padrão).
- Leitura do HA por um `POST /api/template` pequeno, com estados de erro
  (`sem hub`, `sem token`, `token ruim`, `offline`).
- Token longo até **319** caracteres.
- Campainha e botão **Porta**: snapshots JPEG no painel (`POST /camera`).
- Até 5 linhas, no formato `entidade` ou `entidade=Rótulo`. Padrão de fábrica:
  `switch.carros_switch_1=Garagem 1`, `switch.carros_switch_2=Garagem 2`,
  `switch.casa_aracaju_piscina=Piscina`, `switch.casa_ewelink_poco=Poco`.
- Relógio Montserrat com antialiasing, gato calico animado, splash ditherizado.
- Inversão de cor **desligada** por padrão; ordem BGR opcional (vale depois de reiniciar).
- Ajustes da tela na NVS e na seção **Tela do CYD** (`GET`/`POST /config`).
  Inversão e brilho gravados por `POST /config` ou `POST /display` entram na
  próxima passagem da tarefa da tela (`display_task`). O servidor HTTP não
  mexe no painel nem na backlight.
- Ícone de internet na tela de descanso.
- Alertas do HA (`POST /alert`, `GET /alerts`), selo, lista e **LIMPAR TUDO**.
- OTA por HTTP com duas partições e rollback (`POST /ota`).
- Se a tarefa da tela não der uma volta em **45 s** (visor da câmera incluído),
  o CYD reinicia sozinho.
- `GET /touch` para depurar o toque, com `uptime_s`, `disp_age_ms`, `reset`,
  `heap` e `heap_min`.
- `GET /screenshot?key=<chave>` devolve a tela em BMP 24-bit. O HTTP só
  enfileira; quem lê o painel é `display_task`.

## Requisitos

- ESP-IDF **v5.3.2** e a toolchain `esp32`
- `qemu-xtensa` (QEMU da Espressif) só para emular o portal

No Cloud Agent isso é instalado por [`.cursor/install.sh`](.cursor/install.sh).
Manual: [Get Started do ESP-IDF 5.3.2](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32/get-started/index.html).

## Ativar a toolchain

```bash
. "$HOME/esp/esp-idf/export.sh"
```

## Fluxo de Wi-Fi

1. No boot o aparelho lê SSID e senha na NVS.
2. Se existirem, entra como estação e reconecta se a rede cair.
3. Se faltarem, ou a entrada falhar, sobe o SoftAP `ESP32-Setup` e a página de configuração.
4. O celular entra em `ESP32-Setup` (senha `esp32setup`), abre o IP do aparelho e envia a rede.
5. As credenciais vão para a NVS e o aparelho reinicia para conectar.

SSID e senha do AP estão em [`main/wifi_manager.h`](main/wifi_manager.h).

### Endpoints

Servidor em [`main/http_server.c`](main/http_server.c), porta **80**:

- `GET /` — formulário de Wi-Fi, ou o app (chat, Casa, Ajustes) quando já há rede.
- `GET /scan` — redes próximas em JSON. No QEMU (sem rádio) devolve exemplos.
- `POST /connect` — SSID de 1–32 caracteres e senha vazia ou de 8–63; grava na NVS e reinicia.
- `POST /forget` — apaga o Wi-Fi salvo.
- `GET /state`, `GET /bots`, `GET /devices`, `GET /hub/discover`, `GET /weather`
- `POST /chat`, `POST /hub`, `POST /bot`
- `GET /config`, `POST /config` — ajustes da tela (NVS `app_cfg`). O que mexe
  no painel (inversão, brilho) só é aplicado na próxima passagem de `display_task`.
- `POST /camera` — abre o visor de snapshot (exige a chave do aparelho)
- `POST /display` — inversão, BGR e padrão de teste (exige a chave). A inversão
  entra na próxima passagem de `display_task`; BGR só depois de reiniciar.
- `POST /alert`, `GET /alerts` — alertas do HA (POST exige a chave)
- `GET /touch` — última leitura do XPT2046, mais `uptime_s`, `disp_age_ms`
  (ms desde a última volta de `display_task`, ou `-1`), `reset`
  (`esp_reset_reason`) e `heap` / `heap_min`
- `POST /ota` — atualização HTTP (exige a chave)
- `POST /reboot?key=<chave>` — reinicia a placa cerca de 1,5 s depois da
  resposta `{"ok":true,"reboot":true}` (exige a chave)
- `GET /screenshot?key=<chave>` — BMP 24-bit de 320×240 (230 454 bytes),
  resposta em pedaços, linhas de baixo para cima. A mesma checagem de chave
  de `/reboot`: sem a chave, ou com a chave errada, `403` e
  `{"ok":false,"error":"key"}`. O handler só enfileira o pedido.
  `display_task` lê uma linha por vez (do sprite, se houver um; senão do
  painel) para um buffer compartilhado de 2,5 KB e devolve por fila. Sem
  quadro inteiro na RAM. Se a primeira faixa não chega em cerca de 3 s, a
  resposta é `503`. Durante a leitura o batimento do watchdog (45 s) continua.

A chave é os **16 primeiros hex** de `SHA-256` do token longo do HA. O guia
mostra como calculá-la e como o HA chama `/camera`, `/alert` e `/screenshot`.

No Home Assistant, um `shell_command` grava o BMP em
`/config/www/cyd/screen.bmp`, o botão **CYD: capturar tela** dispara esse
comando e uma câmera `local_file` mostra o arquivo. O pacote está em
[docs/homeassistant/cyd.yaml](docs/homeassistant/cyd.yaml).

## App web

A página ([`main/app.html`](main/app.html)) é feita para um painel pequeno
(cerca de 320×240): alvos de 48 px, tipo de 16 px, abas embaixo e uma coluna.
No celular ela cresce.

- **Chat** com o gatinho. As frases vão para o Assist do HA.
- **Casa** lista as entidades configuradas na tela.
- **Ajustes** mostra a rede e a seção **Tela do CYD** (dispositivos, câmeras,
  brilho, cores, gato, relógio, alertas) e o envio de `esp32-ota.bin`.
- Depois de **30 s** sem toque, o protetor cobre a página. `prefers-reduced-motion`
  desliga a animação do gato no navegador.

No primeiro uso o onboarding pergunta o hub. Endereço de exemplo, porta 80:
`http://192.168.0.10`. O token longo vai no campo de senha.

## Cheap Yellow Display (ESP32-2432S028)

Paisagem 320×240 (ILI9341 + XPT2046). A tabela de partições tem dois slots OTA
de 1,94 MB. Quem já gravou uma imagem antiga precisa de **um último flash por
cabo** ([INSTALACAO.md](docs/INSTALACAO.md)); as próximas trocas são `POST /ota`.

```bash
idf.py -B build_cyd -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.cyd" build
idf.py -B build_cyd -p /dev/ttyUSB0 flash monitor
```

Porta serial: `/dev/ttyUSB0` ou `/dev/ttyACM0` no Linux, `COMx` no Windows,
`/dev/cu.usbserial-*` ou `/dev/cu.wchusbserial*` no macOS. Esta imagem é só a
CYD **2.8"**. A placa de 3.5" usa outro controlador.

Para regenerar `release/cyd/` (incluindo `firmware-cyd.bin` e `esp32-ota.bin`):

```bash
./scripts/pack-release-cyd.sh
```

## Build sem o painel

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Portal no QEMU

O QEMU não emula o rádio Wi-Fi. O build dedicado desliga o rádio
(`CONFIG_APP_ENABLE_WIFI_RADIO=n`, ver [`sdkconfig.qemu`](sdkconfig.qemu)) e
serve o mesmo portal pela Ethernet OpenCores.

```bash
idf.py -B build_qemu -DSDKCONFIG=build_qemu/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu" build

qemu-system-xtensa -M esp32 -m 4M \
  -drive file=build_qemu/flash_image.bin,if=mtd,format=raw \
  -drive file=build_qemu/qemu_efuse.bin,if=none,format=raw,id=efuse \
  -global driver=nvram.esp32.efuse,property=drive,value=efuse \
  -global driver=timer.esp32.timg,property=wdt_disable,value=true \
  -nic user,model=open_eth,hostfwd=tcp::8080-:80 \
  -nographic -serial mon:stdio
```

(`build_qemu/flash_image.bin` e `build_qemu/qemu_efuse.bin` aparecem na primeira
vez que se roda `idf.py -B build_qemu qemu`.)

Abra <http://127.0.0.1:8080/> ou:

```bash
curl http://127.0.0.1:8080/
curl -X POST --data 'ssid=MyHomeWiFi&password=secret' \
     http://127.0.0.1:8080/connect
```

No build QEMU o portal passa a mostrar o estado conectado simulado. Reinicie o
emulador para ver `found stored credentials for SSID '...'`.

## Testes

A lógica do formulário (URL, campos, SSID e senha) está no componente
`wifi_form` e tem testes de host com `gcc` + Unity:

```bash
./host_test/run.sh
```

## CI

O workflow [`.github/workflows/ci.yml`](.github/workflows/ci.yml) faz, em push e
pull request para `main`:

- secret scanning com Gitleaks
- garantia de que `src/secrets.h` não está versionado
- build PlatformIO do firmware de referência
- build **ESP-IDF 5.3.2** do CYD, com artefato `release` contendo
  `firmware-cyd.bin` (imagem única) e `esp32-ota.bin` (payload do `POST /ota`)

## Layout

```
.
├── CMakeLists.txt
├── partitions_ota.csv      # nvs + otadata + ota_0 + ota_1
├── sdkconfig.defaults
├── sdkconfig.cyd
├── sdkconfig.qemu
├── docs/
├── release/cyd/
├── scripts/pack-release-cyd.sh
├── components/
│   ├── esp_lcd_ili9341/
│   └── wifi_form/
├── host_test/
├── main/                   # portal, HA, painel, OTA
├── platformio.ini          # firmware de referência (abaixo)
└── src/                    # firmware de referência
```

## Firmware de referência PlatformIO

Ponto de partida separado (DevKit, sem o painel CYD): Wi-Fi com reconexão,
MQTT (`PubSubClient`, QoS 1, LWT) e OTA Arduino. Credenciais em `src/secrets.h`,
que **não** entra no git. Na primeira `pio run`, `scripts/ensure_secrets.py`
copia `src/secrets.example.h`.

```sh
pio run
pio run -t upload
pio run -t monitor
```

| Constante | Uso |
|-----------|-----|
| `DEVICE_NAME` | ID no broker e no OTA |
| `WIFI_TIMEOUT_MS` | Timeout do Wi-Fi |
| `MQTT_BROKER_HOST` / `PORT` | Broker |
| `MQTT_TOPIC_TELEMETRY` | Telemetria |
| `MQTT_TOPIC_STATUS` | online/offline (LWT) |
| `MQTT_TOPIC_COMMAND` | `led:on`, `led:off`, `led:toggle` |
| `PUBLISH_INTERVAL_MS` | Intervalo da telemetria |

Tópicos: `devices/<nome>/telemetry`, `.../status`, `.../command`.

## Licença

MIT — veja `LICENSE`.
