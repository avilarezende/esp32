/**
 * ====================================================================
 * CONFIGURAÇÃO CENTRAL — ESP32
 * ====================================================================
 * Ajuste aqui os parâmetros do dispositivo. NUNCA coloque senhas aqui:
 * credenciais vão em secrets.h (ignorado pelo git).
 */

#pragma once

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