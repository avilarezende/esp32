# Instalação e gravação no CYD

Guia para Windows e Linux/macOS. Há três caminhos: **imagem única pela web**,
**scripts no cabo** (este é o último flash obrigatório depois da troca de
partições) e **OTA** para as próximas versões.

## Hardware

- Cheap Yellow Display **2.8"** (ESP32-2432S028), USB-C ou micro-USB de **dados**
- Cabo curto e de boa qualidade (cabos só de carga falham no flash)
- Computador com ESP-IDF **5.3.2** (ou o `esptool` via pip) e o driver serial
  (CH340 ou CP210x)

## Partições (leia antes de gravar uma placa que já tinha firmware)

A imagem atual não cabe mais num único app de fábrica em `0x10000`. A tabela
[`partitions_ota.csv`](../partitions_ota.csv) ficou assim:

| Nome | Tipo | Offset | Tamanho |
|------|------|--------|---------|
| `nvs` | dados | `0x9000` | `0x6000` (24 KB) |
| `otadata` | dados | `0xf000` | `0x2000` (8 KB) |
| `ota_0` | app | `0x20000` | `0x1F0000` (~1,94 MB) |
| `ota_1` | app | `0x210000` | `0x1F0000` (~1,94 MB) |

A NVS **não mudou de lugar**. Wi-Fi, token do HA e ajustes da tela sobrevivem
a um flash que não apague `0x9000`.

Quem já rodava o firmware antigo precisa de **um último flash por cabo** para
gravar o novo bootloader, a nova tabela e o app em `ota_0`. O `POST /ota` só
passa a funcionar depois dessa gravação: a tabela antiga não tem dois slots.

O `flash.sh` / `flash.bat` gravam só estes offsets:

- `0x1000` bootloader
- `0x8000` tabela de partições
- `0xf000` otadata em branco (o bootloader escolhe `ota_0`)
- `0x20000` aplicação

Eles **não** escrevem em `0x9000`. Use este caminho no último flash se quiser
manter a configuração.

A imagem única `firmware-cyd.bin`, gravada em `0x0`, preenche os buracos com
`0xFF`. Isso **apaga a NVS**. Serve para uma placa nova ou quando você quer
começar do zero. Não use essa imagem única se a meta for só migrar a tabela
sem perder o Wi-Fi.

## Caminho A — último flash por cabo (preserva a NVS)

1. Baixe a pasta `release/cyd/` (ou o artefato `release` do GitHub Actions).
2. Instale o [ESP-IDF 5.3.2](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32/get-started/)
   (atalho **ESP-IDF PowerShell** no Windows) **ou** o
   [`esptool`](https://pypi.org/project/esptool/) (`pip install esptool`).
3. Ligue o CYD no USB e descubra a porta:

   ```powershell
   python -m serial.tools.list_ports
   ```

   No Windows costuma ser `COM3`, `COM4`, …  
   No Linux: `/dev/ttyUSB0` ou `/dev/ttyACM0`  
   No macOS: `/dev/cu.wchusbserial*` ou `/dev/cu.usbserial*`

4. Grave:

   **Windows** (dentro de `release/cyd`):

   ```bat
   flash.bat COM3
   ```

   **Linux / macOS**:

   ```bash
   chmod +x flash.sh
   ./flash.sh /dev/ttyUSB0
   ```

5. Monitor opcional:

   ```bat
   idf.py -p COM3 monitor
   ```

   Esperado: `config AP up: SSID 'ESP32-Setup'` ou, se a NVS ainda tiver a
   rede, a estação sobe e o status vai a `HA ok`.

## Caminho A2 — um único `.bin` pela web (apaga a NVS)

1. Use [`release/cyd/firmware-cyd.bin`](../release/cyd/firmware-cyd.bin).
2. No Chrome ou Edge, abra <https://espressif.github.io/esptool-js/>.
3. Ligue o CYD no USB → **Connect**.
4. Flash address `0x0`, selecione `firmware-cyd.bin` → **Program**.

Equivale a:

```bash
python -m esptool --chip esp32 -b 460800 write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m 0x0 firmware-cyd.bin
```

Depois é preciso configurar de novo o Wi-Fi e o token.

## Caminho B — compilar

ESP-IDF **v5.3.2**. No Windows, abra sempre o atalho **ESP-IDF PowerShell**.

```powershell
git clone https://github.com/avilarezende/esp32.git
cd esp32
git checkout cursor/cyd-touch-ui-b931
idf.py -B build_cyd -DSDKCONFIG=build_cyd/sdkconfig -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.cyd" fullclean
idf.py -B build_cyd -DSDKCONFIG=build_cyd/sdkconfig -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.cyd" build
idf.py -B build_cyd -DSDKCONFIG=build_cyd/sdkconfig -p COM3 flash monitor
```

O `fullclean` evita um `sdkconfig` antigo sem `CONFIG_APP_ENABLE_CYD`.

Para atualizar a pasta `release/cyd/` (binários, `esp32-ota.bin`, SHA256):

```bash
./scripts/pack-release-cyd.sh
```

O CI faz o mesmo com a imagem Docker `espressif/idf:v5.3.2` e publica o
artefato `release`.

## Caminho C — OTA (sem cabo), depois do flash da tabela nova

1. Calcule a chave do aparelho (16 hex do SHA-256 do token longo):

   ```bash
   printf '%s' 'SEU_TOKEN' | sha256sum | cut -c1-16
   ```

2. No celular, na mesma rede do CYD, abra `http://IP_DO_CYD/` → **Ajustes** →
   **Atualizar firmware**. Informe a chave e escolha `esp32-ota.bin`
   (é o app puro, **não** o `firmware-cyd.bin`).
3. Ou, de um computador:

   ```bash
   curl -X POST --data-binary @esp32-ota.bin \
     "http://192.168.0.50/ota?key=SUA_CHAVE_16"
   ```

   Também aceita `POST /ota?key=...&url=http://servidor/esp32-ota.bin`
   (HTTP, não HTTPS: `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP`).

O CYD grava o outro slot (`ota_0` ↔ `ota_1`), marca essa imagem para boot e
reinicia. O rollback está ligado: se a imagem nova **não** ficar 60 segundos
com Wi-Fi associado, o próximo reboot volta para o slot anterior. Não desligue
a energia no meio do POST.

`esp32-ota.bin` do artefato de CI é o arquivo certo. Gravar `firmware-cyd.bin`
pelo `/ota` falha: aquilo é a flash inteira, não uma imagem de app.

## Primeiro uso na placa

1. No celular, Wi-Fi **`ESP32-Setup`**, senha **`esp32setup`**.
2. Abra **`http://192.168.4.1`**.
3. Escolha a rede de casa e salve (a placa reinicia).
4. No onboarding:
   - Tipo: **Home Assistant**
   - Endereço (porta 80): `http://192.168.0.10`
   - Senha: token longo do HA (até 319 caracteres)
5. Em alguns segundos o painel deve mostrar `HA ok`. Se o HA escutar em 8123,
   use `http://192.168.0.10:8123` no lugar.

O pacote do HA (campainha e alertas) está em
[homeassistant/cyd.yaml](homeassistant/cyd.yaml) e no [GUIA.md](GUIA.md).

## Problemas comuns

| Sintoma | O que fazer |
|---------|-------------|
| `idf.py: command not found` | Abrir o atalho ESP-IDF 5.3.2, não o terminal genérico |
| Porta não aparece | Cabo de dados; driver CH340/CP210x; outro USB |
| Placa reinicia em loop depois do OTA | Esperar o rollback no próximo reboot, ou repetir o flash por cabo |
| App antigo não aceita `/ota` | A tabela velha não tem slot OTA: fazer o flash por cabo uma vez |
| `sem hub` no painel | Configurar o HA no app (token no campo senha) |
| `token ruim` / `offline` | Conferir `http://IP` (porta 80) ou `:8123`, o token, e se os dois estão na mesma LAN |
| Token “não cola” | O campo aceita 319 caracteres; tokens cortados em 63 são de um firmware anterior |
| Cores invertidas | Em **Tela do CYD**, deixar **Inverter cores** desmarcado |
| Tela preta na 3.5" | Esta imagem é só para a CYD **2.8"** |
| `{"ok":false,"error":"key"}` | A chave não é o token: são 16 hex do SHA-256 dele |
