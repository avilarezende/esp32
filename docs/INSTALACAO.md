# Instalação e gravação no CYD

Guia passo a passo para Windows (recomendado neste projeto) e Linux/macOS.
Há dois caminhos: **gravar o pacote pronto** (mais simples) ou **compilar do
código**.

## Hardware

- Cheap Yellow Display **2.8"** (ESP32-2432S028), USB-C ou micro-USB de **dados**
- Cabo curto e de boa qualidade (cabos só de carga falham no flash)
- Computador com ESP-IDF **5.3+** ou **6.x** (para gravar) e driver do chip
  serial (CH340 ou CP210x)

## Caminho A — pacote pronto (sem compilar)

1. Clone ou baixe o repositório e abra a pasta `release/cyd/`.
2. Instale o [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
   (atalho **ESP-IDF PowerShell** / **ESP-IDF CMD** no Windows) **ou** o
   [`esptool`](https://pypi.org/project/esptool/) via `pip install esptool`.
3. Ligue o CYD no USB e descubra a porta:

   ```powershell
   python -m serial.tools.list_ports
   ```

   No Windows costuma ser `COM3`, `COM4`, …  
   No Linux: `/dev/ttyUSB0` ou `/dev/ttyACM0`  
   No macOS: `/dev/cu.wchusbserial*` ou `/dev/cu.usbserial*`

4. Grave:

   **Windows** (PowerShell do ESP-IDF, dentro de `release/cyd`):

   ```bat
   flash.bat COM3
   ```

   **Linux / macOS**:

   ```bash
   chmod +x flash.sh
   ./flash.sh /dev/ttyUSB0
   ```

5. Abra o monitor serial (opcional):

   ```bat
   idf.py -p COM3 monitor
   ```

   Esperado: `config AP up: SSID 'ESP32-Setup'` ou, se já houver Wi-Fi,
   `HA ok` / heartbeats sem `Guru Meditation`.

## Caminho B — compilar do código

### 1. ESP-IDF

Instale o ESP-IDF **v5.3.2** (ou 6.x, compatível com este firmware) seguindo o
[Get Started](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32/get-started/).

No Windows use o instalador oficial e abra sempre o atalho
**ESP-IDF … PowerShell** (o PowerShell comum não tem `idf.py`).

### 2. Código

```powershell
cd $env:USERPROFILE
git clone https://github.com/avilarezende/esp32.git
cd esp32
git checkout cursor/cyd-touch-ui-b931
```

### 3. Build do painel

```powershell
idf.py -B build_cyd -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.cyd" fullclean
idf.py -B build_cyd -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.cyd" build
```

O `fullclean` evita um `sdkconfig` antigo sem `CONFIG_APP_ENABLE_CYD`.

### 4. Flash

```powershell
python -m serial.tools.list_ports
idf.py -B build_cyd -p COM3 flash monitor
```

Troque `COM3` pela porta listada.

## Primeiro uso na placa

1. No celular, conecte ao Wi-Fi **`ESP32-Setup`**, senha **`esp32setup`**.
2. Abra **`http://192.168.4.1`**.
3. Escolha a rede de casa e salve (a placa reinicia).
4. No onboarding:
   - Tipo: **Home Assistant**
   - Endereço: `IP_DO_HA:8123` (ex.: `192.168.0.10:8123`)
   - Senha: *long-lived access token* do HA
5. Em alguns segundos o painel deve mostrar `HA ok`, temperatura/umidade e a
   lista de dispositivos.

## Problemas comuns

| Sintoma | O que fazer |
|---------|-------------|
| `idf.py: command not found` | Abrir o atalho ESP-IDF, não o terminal genérico |
| Porta não aparece | Cabo de dados; driver CH340/CP210x; outro USB |
| Placa reinicia em loop | Atualizar para o branch atual; ver log no `monitor` |
| `sem hub` no painel | Configurar HA no app web (token no campo senha) |
| `token ruim` / `offline` | Conferir IP:8123 e token; HA e CYD na mesma LAN |
| Tela preta na 3.5" | Esta imagem é só para a CYD **2.8"** |

## Regenerar o pacote `release/cyd`

Com o ESP-IDF ativo, na raiz do repositório:

```bash
./scripts/pack-release-cyd.sh
```

Isso recompila `build_cyd` e atualiza `release/cyd/` (binários + SHA256).
