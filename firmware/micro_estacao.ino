/*
 * Micro Estação Meteorológica — Qualidade do Ar
 * Universidade Presbiteriana Mackenzie — ADS 2026
 * Autor: Guilherme Oliveira Carvalho
 *
 * Hardware:
 *   - ESP32 DevKit V1
 *   - Sensor DHT22   → GPIO4  (temperatura e umidade)
 *   - Sensor MQ-135  → GPIO34 (gases / VOCs — leitura analógica)
 *   - Sensor PMS5003 → UART2  TX=GPIO17, RX=GPIO16 (material particulado)
 *   - LED RGB        → R=GPIO25, G=GPIO26, B=GPIO27 (atuador — indicador visual)
 *   - Buzzer passivo → GPIO32 (atuador — alerta sonoro)
 *   - Display OLED   → I2C SDA=GPIO21, SCL=GPIO22
 *
 * Simulação Wokwi:
 *   - PMS5003 substituído por valores simulados no código
 *   - MQ-135 representado por potenciômetro (mesma interface ADC)
 *   - Broker MQTT: broker.emqx.io (público)
 *
 * MQTT Topics (publish):
 *   estacao/sensor/temperatura   — temperatura em °C
 *   estacao/sensor/umidade       — umidade relativa em %
 *   estacao/sensor/co2           — CO₂eq estimado em ppm
 *   estacao/sensor/pm25          — PM2.5 em µg/m³
 *   estacao/sensor/pm10          — PM10 em µg/m³
 *   estacao/status/qualidade     — BOM / MODERADO / RUIM
 *   estacao/status/alerta        — alerta quando qualidade RUIM
 *
 * MQTT Topics (subscribe — controle remoto dos atuadores):
 *   estacao/controle/led         — VERDE / AMARELO / VERMELHO / OFF
 *   estacao/controle/buzzer      — ON / OFF
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

// ── Pinos ──────────────────────────────────────────────────────────────────
#define PIN_DHT22   4
#define PIN_MQ135   34
#define PIN_LED_R   25
#define PIN_LED_G   26
#define PIN_LED_B   27
#define PIN_BUZZER  32

// ── Limites OMS ────────────────────────────────────────────────────────────
#define LIMITE_PM25_BOM   15.0f   // µg/m³
#define LIMITE_PM25_MOD   35.0f   // µg/m³
#define LIMITE_CO2_BOM   800.0f   // ppm
#define LIMITE_CO2_MOD  1500.0f   // ppm

// ── Credenciais WiFi / MQTT ────────────────────────────────────────────────
// Para hardware físico: substitua SSID e PASS pela sua rede
// Para Wokwi: use "Wokwi-GUEST" e ""
const char* SSID   = "Wokwi-GUEST";
const char* PASS   = "";
const char* BROKER = "broker.emqx.io";   // broker público gratuito

// ── Objetos ────────────────────────────────────────────────────────────────
DHT dht(PIN_DHT22, DHT22);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
WiFiClient wc;
PubSubClient mqtt(wc);
unsigned long lastRead = 0;

// ── Atuador: LED RGB ───────────────────────────────────────────────────────
void setLED(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(PIN_LED_R, r);
  analogWrite(PIN_LED_G, g);
  analogWrite(PIN_LED_B, b);
}

// ── Callback MQTT — controle remoto dos atuadores ─────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String msg = "";
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];

  if (String(topic) == "estacao/controle/led") {
    if      (msg == "VERDE")    setLED(0, 255, 0);
    else if (msg == "AMARELO")  setLED(255, 200, 0);
    else if (msg == "VERMELHO") setLED(255, 0, 0);
    else                        setLED(0, 0, 0);
  }
  if (String(topic) == "estacao/controle/buzzer") {
    if (msg == "ON") tone(PIN_BUZZER, 2000, 500);
    else             noTone(PIN_BUZZER);
  }
}

// ── Conexão MQTT com reconexão automática ─────────────────────────────────
void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("MQTT...");
    if (mqtt.connect("estacao_mackenzie_iot")) {
      Serial.println("CONECTADO");
      mqtt.subscribe("estacao/controle/led");
      mqtt.subscribe("estacao/controle/buzzer");
      mqtt.publish("estacao/status/online", "true", true);
    } else {
      Serial.printf(" erro rc=%d\n", mqtt.state());
      delay(3000);
    }
  }
}

// ── Publica valor float em tópico MQTT ────────────────────────────────────
void pub(const char* topic, float val, int dec = 1) {
  char buf[16];
  dtostrf(val, 4, dec, buf);
  mqtt.publish(topic, buf);
}

// ── Leitura DHT22 com fallback robusto para simulador ─────────────────────
// NaN é o único número que não é igual a si mesmo (IEEE 754)
float validDHT(float v, float fallbackMin, float fallbackMax, float vmin, float vmax) {
  if (v != v || v < vmin || v > vmax)
    return fallbackMin + (random(0, (int)((fallbackMax - fallbackMin) * 10))) / 10.0f;
  return v;
}

// ═══════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  setLED(0, 0, 255);   // Azul = inicializando

  dht.begin();
  Wire.begin(21, 22);

  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println("Iniciando...");
  oled.display();

  // Aguarda DHT estabilizar (necessário no hardware físico)
  delay(2000);

  // Conectar WiFi
  WiFi.begin(SSID, PASS);
  Serial.print("WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.println(" OK");

  // Conectar MQTT
  mqtt.setServer(BROKER, 1883);
  mqtt.setCallback(mqttCallback);
  connectMQTT();

  setLED(0, 255, 0);   // Verde = pronto
  Serial.println("Sistema pronto!");
}

// ═══════════════════════════════════════════════════════════════════════════
void loop() {
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();  // processa mensagens recebidas (controle remoto dos atuadores)

  if (millis() - lastRead >= 5000) {
    lastRead = millis();

    // ── Leitura DHT22 ──────────────────────────────────────────────────
    float temp = validDHT(dht.readTemperature(), 22.0f, 27.0f, -40.0f, 80.0f);
    float hum  = validDHT(dht.readHumidity(),    55.0f, 63.0f,   0.0f, 100.0f);

    // ── Leitura MQ-135 via ADC (potenciômetro no Wokwi) ───────────────
    float co2 = 400.0f + (analogRead(PIN_MQ135) / 4095.0f) * 1600.0f;

    // ── PM2.5 / PM10 simulados (PMS5003 indisponível no Wokwi) ────────
    float pm25 = 10.0f + (random(0, 350)) / 10.0f;
    float pm10 = pm25 * 1.4f;

    // ── Classificar qualidade do ar e acionar atuadores ────────────────
    String qualidade;
    if (pm25 <= LIMITE_PM25_BOM && co2 <= LIMITE_CO2_BOM) {
      qualidade = "BOM";
      setLED(0, 255, 0);
      noTone(PIN_BUZZER);
    } else if (pm25 <= LIMITE_PM25_MOD && co2 <= LIMITE_CO2_MOD) {
      qualidade = "MODERADO";
      setLED(255, 200, 0);
      noTone(PIN_BUZZER);
    } else {
      qualidade = "RUIM";
      setLED(255, 0, 0);
      tone(PIN_BUZZER, 2000, 300);
      mqtt.publish("estacao/status/alerta",
        ("ALERTA: PM2.5=" + String(pm25, 1) + " ug/m3 > limite OMS").c_str());
    }

    // ── Publicar no broker MQTT ────────────────────────────────────────
    unsigned long t0 = millis();
    pub("estacao/sensor/temperatura", temp);
    pub("estacao/sensor/umidade",     hum,  0);
    pub("estacao/sensor/co2",         co2,  0);
    pub("estacao/sensor/pm25",        pm25);
    pub("estacao/sensor/pm10",        pm10);
    mqtt.publish("estacao/status/qualidade", qualidade.c_str());
    unsigned long latencia = millis() - t0;

    Serial.printf("[MQTT] T=%.1f H=%.0f CO2=%.0f PM25=%.1f -> %s (%lums)\n",
                  temp, hum, co2, pm25, qualidade.c_str(), latencia);

    // ── Atualizar display OLED ─────────────────────────────────────────
    oled.clearDisplay();
    oled.setCursor(0, 0);
    oled.printf("T:%.1fC  H:%.0f%%\n", temp, hum);
    oled.printf("CO2:%.0f ppm\n", co2);
    oled.printf("PM25:%.1f ug/m3\n", pm25);
    oled.setTextSize(2);
    oled.setCursor(0, 48);
    oled.print(qualidade);
    oled.display();
  }
}
