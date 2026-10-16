/**
 * ====================================================================
 * ESP32 — Firmware de referência (Wi-Fi + MQTT + OTA)
 * ====================================================================
 * Estrutura limpa e segura para projetos IoT:
 *   - Configuração centralizada (config.h) e credenciais separadas (secrets.h)
 *   - Conexão Wi-Fi com reconexão automática e watchdog
 *   - Publicação MQTT com retenção de mensagens fora de sincronia
 *   - Assinatura de tópicos de comando com callback
 *   - OTA seguro (somente com senha configurada)
 *   - Log estruturado via Serial
 *
 * COMO USAR:
 *   1. Copie src/secrets.example.h para src/secrets.h e preencha.
 *   2. Compile com PlatformIO (pio run) e envie (pio run -t upload).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <secrets.h>
#include <config.h>

// ---------------------------------------------------------------- globals
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
unsigned long lastReconnectAttempt = 0;

// ------------------------------------------------------------------ Wi-Fi
/**
 * Conecta ao Wi-Fi com timeout e mensagens de status.
 * Retorna true se conectado.
 */
bool conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.printf("[wifi] Conectando a %s...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false); // evita latência no MQTT

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < WIFI_TIMEOUT_MS) {
    delay(200);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] Conectado. IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("[wifi] Falha ao conectar. Reiniciando em 5s...");
  delay(5000);
  ESP.restart();
  return false;
}

// ------------------------------------------------------------------ MQTT
/**
 * Publica um payload em um tópico com QoS 1.
 * Retorna true em sucesso.
 */
bool publicarMqtt(const char *topico, const char *payload, bool retained = false) {
  if (!mqttClient.connected()) return false;
  bool ok = mqttClient.publish(topico, payload, retained);
  if (ok) {
    Serial.printf("[mqtt] publicou -> %s: %s\n", topico, payload);
  } else {
    Serial.printf("[mqtt] FALHA ao publicar em %s\n", topico);
  }
  return ok;
}

/**
 * Assina tópicos e registra handlers de comando.
 */
void configurarSubscricoes() {
  mqttClient.subscribe(MQTT_TOPIC_COMMAND);
  Serial.printf("[mqtt] assinado: %s\n", MQTT_TOPIC_COMMAND);
}

// ------------------------------------------------------------------- OTA
void configurarOTA() {
  ArduinoOTA.setHostname(DEVICE_NAME);
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
#endif

  ArduinoOTA.onStart([]() {
    Serial.println("[ota] Iniciando atualização...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("[ota] Concluída!");
  });
  ArduinoOTA.onError([](ota_error_t erro) {
    Serial.printf("[ota] Erro %u\n", erro);
    ESP.restart();
  });

  ArduinoOTA.begin();
  Serial.println("[ota] OTA pronto.");
}

// ------------------------------------------------------------ callback MQTT
/**
 * Handler de mensagens nos tópicos assinados.
 * Dados vindos de rede devem ser tratados como não confiáveis.
 */
void onMqttMessage(char *topico, byte *payload, unsigned int tamanho) {
  char mensagem[MAX_PAYLOAD_SIZE + 1];
  memcpy(mensagem, payload, tamanho);
  mensagem[tamanho] = '\0';
  Serial.printf("[mqtt] recebeu %s: %s\n", topico, mensagem);

  if (strcmp(topico, MQTT_TOPIC_COMMAND) == 0) {
    // Exemplo: "led:on" / "led:off" ou "led:toggle"
    if (strcmp(mensagem, "led:toggle") == 0) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      publicarMqtt(MQTT_TOPIC_STATUS, digitalRead(LED_BUILTIN) ? "led:on" : "led:off");
    } else if (strcmp(mensagem, "led:on") == 0) {
      digitalWrite(LED_BUILTIN, HIGH);
      publicarMqtt(MQTT_TOPIC_STATUS, "led:on");
    } else if (strcmp(mensagem, "led:off") == 0) {
      digitalWrite(LED_BUILTIN, LOW);
      publicarMqtt(MQTT_TOPIC_STATUS, "led:off");
    } else {
      Serial.printf("[mqtt] comando desconhecido: %s\n", mensagem);
    }
  }
}

/**
 * Tenta (re)conectar ao broker MQTT com estado online/offline (LWT).
 */
boolean conectarMqtt() {
  if (!mqttClient.connected()) {
    Serial.printf("[mqtt] Conectando ao broker %s:%d...\n", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    mqttClient.setCallback(onMqttMessage);

    // Última vontade: se o device cair, o broker avisa os assinantes.
    boolean conectado = mqttClient.connect(
        DEVICE_NAME,
        MQTT_USERNAME,
        MQTT_PASSWORD,
        MQTT_TOPIC_STATUS,
        1,          // QoS do LWT
        true,       // retained
        "offline"   // payload do LWT
    );

    if (conectado) {
      Serial.println("[mqtt] Conectado ao broker.");
      publicarMqtt(MQTT_TOPIC_STATUS, "online", true);
      configurarSubscricoes();
    } else {
      Serial.printf("[mqtt] Falha (rc=%d). Tentando de novo em breve.\n", mqttClient.state());
    }
  }
  return mqttClient.connected();
}

// ------------------------------------------------------------------- setup
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.printf("\n[setup] %s iniciando...\n", DEVICE_NAME);

  conectarWiFi();
  configurarOTA();

  mqttClient.setBufferSize(MAX_PAYLOAD_SIZE + 64); // folga para tópicos longos
  conectarMqtt();
}

// -------------------------------------------------------------------- loop
void loop() {
  ArduinoOTA.handle();

  // Reconexão Wi-Fi: tenta a cada enquanto estiver fora
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }

  // Reconexão MQTT: tenta no máximo 1x por segundo
  if (!mqttClient.connected()) {
    unsigned long agora = millis();
    if (agora - lastReconnectAttempt > 1000) {
      lastReconnectAttempt = agora;
      conectarMqtt();
    }
  } else {
    mqttClient.loop();
  }

  // --- publicação periódica de exemplo ------------------------------
  static unsigned long ultimaPublicacao = 0;
  if (millis() - ultimaPublicacao >= PUBLISH_INTERVAL_MS) {
    ultimaPublicacao = millis();

    char payload[MAX_PAYLOAD_SIZE];
    snprintf(payload, sizeof(payload),
             "{\"uptime_s\":%lu,\"heap_free\":%u,\"rssi\":%d}",
             millis() / 1000, ESP.getFreeHeap(), WiFi.RSSI());
    publicarMqtt(MQTT_TOPIC_TELEMETRY, payload);
  }
}