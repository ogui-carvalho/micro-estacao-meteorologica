/*
 * Micro Estação Meteorológica — Qualidade do Ar
 * Universidade Presbiteriana Mackenzie — ADS 2026
 * Autor: Raphael Dionisio Vieira de Figueiredo Lima
 *
 * Hardware:
 *   - ESP32 DevKit V1
 *   - Sensor DHT22   → GPIO4  (temperatura e umidade)
 *   - Sensor MQ-135  → GPIO34 (gases / VOCs — leitura analógica)
 *   - Sensor PMS5003 → UART2  TX=GPIO17, RX=GPIO16 (material particulado)
 *   - LED RGB        → R=GPIO25, G=GPIO26, B=GPIO27 (atuador — indicador visual)
 *   - Buzzer         → GPIO32 (atuador — alerta sonoro)
 *   - Display OLED   → I2C SDA=GPIO21, SCL=GPIO22
 *
 * MQTT Topics (publish):
 *   estacao/sensor/temperatura, estacao/sensor/umidade,
 *   estacao/sensor/co2, estacao/sensor/pm25, estacao/sensor/pm10,
 *   estacao/status/qualidade, estacao/status/alerta
 *
 * MQTT Topics (subscribe):
 *   estacao/controle/buzzer  → "ON" / "OFF"
 *   estacao/controle/led     → "VERDE" / "AMARELO" / "VERMELHO" / "OFF"
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

// ── WiFi ─────────────────────────────────────────────────────────────────────
const char* SSID     = "SUA_REDE_WIFI";
const char* PASSWORD = "SUA_SENHA_WIFI";

// ── MQTT ─────────────────────────────────────────────────────────────────────
const char* MQTT_BROKER = "broker.emqx.io";  // broker público (substitua pelo local)
const int   MQTT_PORT   = 1883;
const char* MQTT_CLIENT = "estacao_ar_mackenzie";

// ── Pinos ─────────────────────────────────────────────────────────────────────
#define PIN_DHT22   4
#define PIN_MQ135   34
#define PIN_LED_R   25
#define PIN_LED_G   26
#define PIN_LED_B   27
#define PIN_BUZZER  32

// ── Tópicos MQTT ─────────────────────────────────────────────────────────────
#define TOPIC_TEMP      "estacao/sensor/temperatura"
#define TOPIC_HUM       "estacao/sensor/umidade"
#define TOPIC_CO2       "estacao/sensor/co2"
#define TOPIC_PM25      "estacao/sensor/pm25"
#define TOPIC_PM10      "estacao/sensor/pm10"
#define TOPIC_STATUS    "estacao/status/qualidade"
#define TOPIC_ALERTA    "estacao/status/alerta"
#define TOPIC_CTRL_BUZ  "estacao/controle/buzzer"
#define TOPIC_CTRL_LED  "estacao/controle/led"

// ── Limites OMS ──────────────────────────────────────────────────────────────
#define LIMITE_PM25_BOM    15.0f   // µg/m³ — abaixo: BOM
#define LIMITE_PM25_MOD    35.0f   // µg/m³ — abaixo: MODERADO, acima: RUIM
#define LIMITE_CO2_BOM     800.0f  // ppm
#define LIMITE_CO2_MOD    1500.0f  // ppm

// ── Objetos ───────────────────────────────────────────────────────────────────
DHT dht(PIN_DHT22, DHT22);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

// ── Estado ────────────────────────────────────────────────────────────────────
unsigned long ultimaLeitura = 0;
const long    INTERVALO_MS  = 5000;
bool buzzerAtivo = false;

// ─────────────────────────────────────────────────────────────────────────────
// Callback MQTT (mensagens recebidas)
// ─────────────────────────────────────────────────────────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  Serial.printf("[MQTT] %s → %s\n", topic, msg.c_str());

  if (String(topic) == TOPIC_CTRL_BUZ) {
    buzzerAtivo = (msg == "ON");
    if (!buzzerAtivo) noTone(PIN_BUZZER);

  } else if (String(topic) == TOPIC_CTRL_LED) {
    if      (msg == "VERDE")    setLED(0, 255, 0);
    else if (msg == "AMARELO")  setLED(255, 200, 0);
    else if (msg == "VERMELHO") setLED(255, 0, 0);
    else                        setLED(0, 0, 0);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
void setLED(int r, int g, int b) {
  analogWrite(PIN_LED_R, r);
  analogWrite(PIN_LED_G, g);
  analogWrite(PIN_LED_B, b);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lê MQ-135 e estima CO2eq em ppm (calibração simplificada)
// ─────────────────────────────────────────────────────────────────────────────
float lerMQ135() {
  int raw = analogRead(PIN_MQ135);          // 0–4095 (12-bit ADC)
  float voltage = raw * 3.3f / 4095.0f;
  // Conversão empírica: Rs/Ro, curva logarítmica simplificada
  float ppm = 400.0f + (voltage / 3.3f) * 1600.0f;
  return ppm;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lê PMS5003 via UART2 (Serial2)
// Retorna PM2.5 e PM10; false se timeout
// ─────────────────────────────────────────────────────────────────────────────
bool lerPMS5003(float &pm25, float &pm10) {
  uint8_t buf[32];
  unsigned long t0 = millis();

  while (Serial2.available() < 32) {
    if (millis() - t0 > 1000) return false;
  }
  Serial2.readBytes(buf, 32);

  if (buf[0] != 0x42 || buf[1] != 0x4D) return false;

  pm25 = ((buf[12] << 8) | buf[13]) * 1.0f;
  pm10 = ((buf[14] << 8) | buf[15]) * 1.0f;
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Classifica qualidade do ar e aciona atuadores
// ─────────────────────────────────────────────────────────────────────────────
String classificarQualidade(float pm25, float co2) {
  if (pm25 <= LIMITE_PM25_BOM && co2 <= LIMITE_CO2_BOM) {
    setLED(0, 255, 0);       // Verde — BOM
    if (buzzerAtivo) noTone(PIN_BUZZER);
    return "BOM";
  } else if (pm25 <= LIMITE_PM25_MOD && co2 <= LIMITE_CO2_MOD) {
    setLED(255, 200, 0);     // Amarelo — MODERADO
    if (buzzerAtivo) noTone(PIN_BUZZER);
    return "MODERADO";
  } else {
    setLED(255, 0, 0);       // Vermelho — RUIM
    if (buzzerAtivo) tone(PIN_BUZZER, 2000, 500);
    return "RUIM";
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Conexão WiFi
// ─────────────────────────────────────────────────────────────────────────────
void conectarWiFi() {
  Serial.printf("Conectando WiFi: %s", SSID);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi OK — IP: %s\n", WiFi.localIP().toString().c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Conexão / reconexão MQTT
// ─────────────────────────────────────────────────────────────────────────────
void conectarMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Conectando MQTT...");
    if (mqtt.connect(MQTT_CLIENT)) {
      Serial.println(" OK");
      mqtt.subscribe(TOPIC_CTRL_BUZ);
      mqtt.subscribe(TOPIC_CTRL_LED);
      mqtt.publish("estacao/status/online", "true", true);
    } else {
      Serial.printf(" falha (rc=%d) — tentando em 3s\n", mqtt.state());
      delay(3000);
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Publica valor float em tópico MQTT
// ─────────────────────────────────────────────────────────────────────────────
void publicar(const char* topic, float valor, int casas = 1) {
  char buf[16];
  dtostrf(valor, 4, casas, buf);
  mqtt.publish(topic, buf);
  Serial.printf("  [PUB] %s = %s\n", topic, buf);
}

// ─────────────────────────────────────────────────────────────────────────────
// Atualiza display OLED
// ─────────────────────────────────────────────────────────────────────────────
void atualizarOLED(float temp, float hum, float co2, float pm25, String status) {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.printf("Temp: %.1f C  Hum: %.0f%%\n", temp, hum);
  oled.printf("CO2:  %.0f ppm\n", co2);
  oled.printf("PM2.5: %.1f ug/m3\n", pm25);
  oled.setTextSize(2);
  oled.setCursor(0, 48);
  oled.print(status);
  oled.display();
}

// ═════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);   // PMS5003

  pinMode(PIN_LED_R,  OUTPUT);
  pinMode(PIN_LED_G,  OUTPUT);
  pinMode(PIN_LED_B,  OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  setLED(0, 0, 255);  // Azul durante inicialização

  dht.begin();
  Wire.begin(21, 22);

  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED não encontrado");
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println("Iniciando...");
  oled.display();

  conectarWiFi();

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  conectarMQTT();

  setLED(0, 255, 0);  // Verde — pronto
  Serial.println("Sistema iniciado.");
}

// ═════════════════════════════════════════════════════════════════════════════
void loop() {
  if (!mqtt.connected()) conectarMQTT();
  mqtt.loop();

  unsigned long agora = millis();
  if (agora - ultimaLeitura >= INTERVALO_MS) {
    ultimaLeitura = agora;

    // ── Leitura sensores ─────────────────────────────────────────────────
    float temp = dht.readTemperature();
    float hum  = dht.readHumidity();
    float co2  = lerMQ135();

    float pm25 = 0.0f, pm10 = 0.0f;
    if (!lerPMS5003(pm25, pm10)) {
      // Wokwi: usa valores simulados quando PMS5003 não responde
      pm25 = 12.0f + (random(0, 30)) / 10.0f;
      pm10 = pm25 * 1.4f;
    }

    if (isnan(temp) || isnan(hum)) {
      Serial.println("Erro DHT22");
      return;
    }

    Serial.printf("\n[LEITURA] T=%.1f°C H=%.0f%% CO2=%.0fppm PM2.5=%.1f PM10=%.1f\n",
                  temp, hum, co2, pm25, pm10);

    // ── Classificação e atuadores ────────────────────────────────────────
    String qualidade = classificarQualidade(pm25, co2);

    // ── Publicação MQTT ──────────────────────────────────────────────────
    unsigned long t0 = millis();

    publicar(TOPIC_TEMP,  temp,  1);
    publicar(TOPIC_HUM,   hum,   0);
    publicar(TOPIC_CO2,   co2,   0);
    publicar(TOPIC_PM25,  pm25,  1);
    publicar(TOPIC_PM10,  pm10,  1);
    mqtt.publish(TOPIC_STATUS, qualidade.c_str());

    if (qualidade == "RUIM") {
      String alerta = "ALERTA: PM2.5=" + String(pm25, 1) + " ug/m3 ACIMA DO LIMITE OMS";
      mqtt.publish(TOPIC_ALERTA, alerta.c_str());
    }

    unsigned long latencia = millis() - t0;
    Serial.printf("[MQTT] Publicação concluída em %lu ms\n", latencia);

    // ── Atualização display ───────────────────────────────────────────────
    atualizarOLED(temp, hum, co2, pm25, qualidade);
  }
}
