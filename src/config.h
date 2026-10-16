/**
 * ====================================================================
 * CONFIGURAÇÃO CENTRAL — ESP32
 * ====================================================================
 * Ajuste aqui os parâmetros do dispositivo. NUNCA coloque senhas aqui:
 * credenciais vão em secrets.h (ignorado pelo git).
 */

#pragma once

// Generic ESP32 DevKit boards do not always define an on-board LED pin.
// GPIO 2 is the standard DevKit LED; board definitions can override it.
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// Identificação do dispositivo no broker e no OTA
#define DEVICE_NAME          "esp32-calisto-01"

// Timeout de conexão Wi-Fi (ms)
#define WIFI_TIMEOUT_MS      20000

// Broker MQTT
#define MQTT_BROKER_HOST     "192.168.1.10"
#define MQTT_BROKER_PORT     1883

// Tópicos MQTT (organizados por dispositivo/telemetria)
#define MQTT_TOPIC_TELEMETRY "devices/esp32-calisto-01/telemetry"
#define MQTT_TOPIC_STATUS    "devices/esp32-calisto-01/status"
#define MQTT_TOPIC_COMMAND   "devices/esp32-calisto-01/command"

// Intervalo de publicação de telemetria (ms)
#define PUBLISH_INTERVAL_MS  30000

// Tamanho máximo de payload MQTT recebido
#define MAX_PAYLOAD_SIZE     256
