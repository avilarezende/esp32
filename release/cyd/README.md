# Pacote pronto — CYD 2.8"

Binários do firmware com painel ILI9341, portal Wi-Fi, Home Assistant, câmera,
alertas e OTA. Gerados com ESP-IDF **5.3.2** (`scripts/pack-release-cyd.sh` ou
o artefato `release` do GitHub Actions).

## Qual arquivo usar

| Arquivo | Quando |
|---------|--------|
| `flash.sh` / `flash.bat` | **Último flash por cabo** numa placa que já tem Wi-Fi e token. Não apaga a NVS. |
| `firmware-cyd.bin` | Imagem única em `0x0` (web). Apaga a NVS porque o buraco entre os pedaços é preenchido com `0xFF`. |
| `esp32-ota.bin` | Só o app, para `POST /ota` **depois** que a tabela nova já está na placa. Não grave este arquivo em `0x0`. |

## Um único arquivo (gravação pela web)

1. Abra no Chrome/Edge: <https://espressif.github.io/esptool-js/>
2. Conecte o CYD no USB → **Connect**.
3. Em *Flash Address* use `0x0` e selecione `firmware-cyd.bin`.
4. **Program**.

Isso regrava do endereço 0 até o fim do app, inclusive a NVS. A placa volta ao
portal `ESP32-Setup`.

## Conteúdo

| Arquivo | Offset | Função |
|---------|--------|--------|
| `firmware-cyd.bin` | `0x0` | Imagem completa (apaga a NVS) |
| `bootloader.bin` | `0x1000` | Bootloader |
| `partition-table.bin` | `0x8000` | Tabela `nvs` + `otadata` + `ota_0` + `ota_1` |
| `otadata.bin` | `0xf000` | 8 KB `0xFF`: sobe o `ota_0` |
| `esp32.bin` | `0x20000` | Aplicação no slot `ota_0` |
| `esp32-ota.bin` | — | O mesmo app, para `POST /ota` |
| `flash.bat` / `flash.sh` | — | Grava os quatro offsets e preserva a NVS |
| `SHA256SUMS` | — | Checksums |

## Gravar pelo terminal (preserva Wi-Fi, token e ajustes)

```bat
flash.bat COM3
```

```bash
chmod +x flash.sh
./flash.sh /dev/ttyUSB0
```

Ou a imagem única, sabendo que a NVS some:

```bash
python -m esptool --chip esp32 -b 460800 write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m 0x0 firmware-cyd.bin
```

## OTA

Com a tabela nova já gravada e o CYD na rede:

```bash
curl -X POST --data-binary @esp32-ota.bin \
  "http://IP_DO_CYD/ota?key=CHAVE16"
```

A chave são os 16 primeiros hex de `sha256(token longo)`. Detalhes em
[docs/INSTALACAO.md](../../docs/INSTALACAO.md) e [docs/GUIA.md](../../docs/GUIA.md).

## Depois do flash

1. Se a NVS foi preservada e já havia rede, o painel segue sozinho.
2. Se não: Wi-Fi `ESP32-Setup` / `esp32setup` → `http://192.168.4.1`.
3. Home Assistant em **porta 80**: endereço `http://192.168.0.10` e o token longo.
4. Painel em `HA ok`; 30 s → relógio (globo de internet); 40 min → tela off.
   O globo azul é internet (HTTP 204); o cinza com X é sem internet. O chip
   `HA ok` é outra coisa.
