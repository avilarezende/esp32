# ESP32 — Firmware de Referência (Wi-Fi + MQTT + OTA)

Firmware base para projetos IoT com ESP32, estruturado e seguro. Serve como
ponto de partida para sensores, automação ou telemetria com:

- **Wi-Fi** com reconexão automática e watchdog
- **MQTT** (`PubSubClient`) com QoS 1, LWT (última vontade) e tópicos separados
  por telemetria / status / comando
- **OTA** seguro via ArduinoOTA
- **Configuração centralizada** — valores em `src/config.h`, credenciais em
  `src/secrets.h` (fora do git)

## Estrutura

```
esp32/
├── platformio.ini          # Config do PlatformIO (board esp32dev)
├── src/
│   ├── main.cpp            # Firmware principal
│   ├── config.h            # Configuração do dispositivo (não sensível)
│   ├── secrets.example.h   # Template de credenciais (versionado)
│   └── secrets.h           # Credenciais reais (gerado/criado, NÃO versionar)
└── scripts/
    └── ensure_secrets.py   # Cria secrets.h a partir do template na 1ª build
```

## Pré-requisitos

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation.html)
  (`pip install platformio` ou via VS Code extension)
- Um ESP32 (DevKit) conectado por USB

## Primeira compilação

```sh
pio run
```

Na primeira build, `src/secrets.h` é criado automaticamente a partir de
`secrets.example.h`. **Abra o arquivo e preencha:**

```c
#define WIFI_SSID       "minha-rede"
#define WIFI_PASSWORD   "minha-senha"
#define MQTT_USERNAME   ""          // se o broker não exige auth
#define MQTT_PASSWORD   ""
#define OTA_PASSWORD     "senha-ota"  // recomendo definir
```

> ⚠️ **Segurança:** `src/secrets.h` está no `.gitignore` e nunca deve ser
> commitado. Se você usa o exemplo como está, o dispositivo não conecta —
> valores reais são obrigatórios.

## Upload

```sh
pio run -t upload      # envia via USB
pio run -t monitor     # monitora o serial (115200 baud)
```

## Configuração (src/config.h)

Ajuste o identificador do dispositivo e o broker antes do upload:

| Constante                 | Uso                                        |
|---------------------------|--------------------------------------------|
| `DEVICE_NAME`             | ID no broker e no OTA                      |
| `WIFI_TIMEOUT_MS`         | Timeout de conexão Wi-Fi                   |
| `MQTT_BROKER_HOST/PORT`   | Endereço e porta do broker MQTT            |
| `MQTT_TOPIC_TELEMETRY`    | Tópico de telemetria periódica             |
| `MQTT_TOPIC_STATUS`       | Tópico de status (online/offline via LWT)  |
| `MQTT_TOPIC_COMMAND`      | Tópico de comandos recebidos              |
| `PUBLISH_INTERVAL_MS`     | Intervalo da telemetria                    |

## Tópicos MQTT (exemplo)

```text
devices/<nome>/telemetry   → JSON com uptime, heap livre e RSSI
devices/<nome>/status      → "online" / "offline" (retido + LWT)
devices/<nome>/command     → comandos: led:on | led:off | led:toggle
```

## Próximos passos sugeridos

- Adicionar sensores (DHT22, DS18B20, etc.) e publicar leituras na telemetria
- Implementar deep sleep para economia de bateria
- Envolver o broker em TLS (configurar CA em `WiFiClientSecure`)
- Adicionar testes unitários com `pio test`

## Licença

MIT — veja `LICENSE`.