# Pacote pronto — CYD 2.8"

Binários pré-compilados do firmware com painel ILI9341, portal Wi-Fi e
observação do Home Assistant.

## Um único arquivo (gravação pela web)

Use **`firmware-cyd.bin`**: imagem completa (bootloader + partições + app)
para gravar no offset **`0x0`**.

1. Abra no Chrome/Edge: <https://espressif.github.io/esptool-js/>
2. Conecte o CYD no USB → **Connect** → escolha a porta serial.
3. Em *Flash Address* use `0x0` e selecione `firmware-cyd.bin`.
4. **Program** e aguarde o fim.

Alternativa: [ESP Web Tools](https://esphome.github.io/esp-web-tools/) ou
qualquer flasher web que aceite um `.bin` em `0x0`.

## Conteúdo

| Arquivo | Função |
|---------|--------|
| `firmware-cyd.bin` | **Imagem única** (offset `0x0`) — preferida para web |
| `bootloader.bin` | Bootloader (offset `0x1000`) |
| `partition-table.bin` | Tabela de partições (`0x8000`) |
| `esp32.bin` | Só a aplicação (`0x10000`) |
| `flash.bat` | Gravação no Windows (três arquivos) |
| `flash.sh` | Gravação no Linux/macOS |
| `SHA256SUMS` | Checksums dos binários |

## Gravar pelo terminal

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

Ou só a imagem única:

```bash
python -m esptool --chip esp32 -b 460800 write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m 0x0 firmware-cyd.bin
```

Documentação completa: [docs/INSTALACAO.md](../../docs/INSTALACAO.md) e
[docs/GUIA.md](../../docs/GUIA.md).

## Depois do flash

1. Wi-Fi `ESP32-Setup` / `esp32setup`
2. `http://192.168.4.1` → rede de casa + Home Assistant (`host:8123` + token)
3. Painel mostra a observação; 30 s → relógio; 40 min → tela off
