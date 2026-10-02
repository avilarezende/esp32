# Pacote pronto — CYD 2.8"

Binários pré-compilados do firmware com painel ILI9341, portal Wi-Fi e
observação do Home Assistant.

## Conteúdo

| Arquivo | Função |
|---------|--------|
| `bootloader.bin` | Bootloader (offset `0x1000`) |
| `partition-table.bin` | Tabela de partições (`0x8000`) |
| `esp32.bin` | Aplicação (`0x10000`) |
| `flash.bat` | Gravação no Windows |
| `flash.sh` | Gravação no Linux/macOS |
| `SHA256SUMS` | Checksums dos binários |

## Gravar

1. Ligue o CYD 2.8" no USB.
2. Descubra a porta: `python -m serial.tools.list_ports`
3. Com ESP-IDF ou `esptool` no PATH:

```bat
flash.bat COM3
```

```bash
chmod +x flash.sh
./flash.sh /dev/ttyUSB0
```

Documentação completa: [docs/INSTALACAO.md](../../docs/INSTALACAO.md) e
[docs/GUIA.md](../../docs/GUIA.md).

## Depois do flash

1. Wi-Fi `ESP32-Setup` / `esp32setup`
2. `http://192.168.4.1` → rede de casa + Home Assistant (`host:8123` + token)
3. Painel mostra a observação; 30 s → relógio; 40 min → tela off
