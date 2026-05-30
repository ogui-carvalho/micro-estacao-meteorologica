# Micro Estação Meteorológica com ESP32 e MQTT

**Universidade Presbiteriana Mackenzie — Análise e Desenvolvimento de Sistemas — 2026**  
**Autor:** Raphael Dionisio Vieira de Figueiredo Lima  
**ODS 11:** Cidades e Comunidades Sustentáveis

---

## Descrição

Estação inteligente de monitoramento da qualidade do ar baseada no microcontrolador **ESP32**, com integração a plataforma **IoT via protocolo MQTT**. O sistema monitora PM2.5, PM10, CO₂eq, temperatura e umidade em tempo real, aciona atuadores (LED RGB e buzzer) conforme a qualidade do ar e transmite os dados para um dashboard Node-RED.

---

## Hardware Utilizado

| Componente | Função | Interface |
|-----------|--------|-----------|
| ESP32 DevKit V1 | Microcontrolador central | — |
| PMS5003 | Sensor PM1.0 / PM2.5 / PM10 | UART (GPIO16/17) |
| MQ-135 | Sensor gases / CO₂eq / VOCs | ADC (GPIO34) |
| DHT22 | Sensor temperatura e umidade | Digital (GPIO4) |
| LED RGB (cátodo comum) | Atuador — indicador visual de qualidade | PWM (GPIO25/26/27) |
| Buzzer passivo | Atuador — alerta sonoro | PWM (GPIO32) |
| Display OLED 0.96" SSD1306 | Visualização local | I2C (GPIO21/22) |
| Fonte 5V / 2A | Alimentação | USB |

### Diagrama de Conexões

```
ESP32 GPIO4   ──── DHT22 DATA (+ pull-up 10kΩ para 3V3)
ESP32 GPIO34  ──── MQ-135 AOUT
ESP32 GPIO16  ──── PMS5003 TX
ESP32 GPIO17  ──── PMS5003 RX
ESP32 GPIO25  ──── LED RGB R (+ resistor 220Ω)
ESP32 GPIO26  ──── LED RGB G (+ resistor 220Ω)
ESP32 GPIO27  ──── LED RGB B (+ resistor 220Ω)
ESP32 GPIO32  ──── Buzzer +
ESP32 GPIO21  ──── OLED SDA
ESP32 GPIO22  ──── OLED SCL
ESP32 3V3     ──── VCC sensores / OLED
ESP32 GND     ──── GND todos componentes
```

---

## Software

### Dependências Arduino (Library Manager)

```
PubSubClient        v2.8+   (MQTT client)
DHT sensor library  v1.4+   (DHT22)
Adafruit SSD1306    v2.5+   (Display OLED)
ArduinoJson         v6.x    (Serialização JSON)
```

### Configuração

Edite `firmware/micro_estacao.ino` e ajuste:

```cpp
const char* SSID     = "SUA_REDE_WIFI";
const char* PASSWORD = "SUA_SENHA_WIFI";
const char* MQTT_BROKER = "broker.emqx.io";  // ou IP do broker local
```

### Upload

1. Instale o [Arduino IDE 2.x](https://www.arduino.cc/en/software)
2. Adicione o ESP32 board: `https://dl.espressif.com/dl/package_esp32_index.json`
3. Instale as bibliotecas listadas acima
4. Selecione **ESP32 Dev Module**, porta correta
5. Clique em **Upload**

---

## Protocolo MQTT

### Broker

- **Local:** Mosquitto — `localhost:1883`
- **Público (simulador):** `broker.emqx.io:1883`

### Tópicos publicados (ESP32 → Broker)

| Tópico | Payload | Exemplo |
|--------|---------|---------|
| `estacao/sensor/temperatura` | float °C | `24.5` |
| `estacao/sensor/umidade` | float % | `58` |
| `estacao/sensor/co2` | float ppm | `412` |
| `estacao/sensor/pm25` | float µg/m³ | `18.3` |
| `estacao/sensor/pm10` | float µg/m³ | `25.6` |
| `estacao/status/qualidade` | string | `BOM` / `MODERADO` / `RUIM` |
| `estacao/status/alerta` | string | `ALERTA: PM2.5=36.1 ug/m3 ACIMA DO LIMITE OMS` |

### Tópicos subscritos (Broker → ESP32 — controle remoto)

| Tópico | Payload | Ação |
|--------|---------|------|
| `estacao/controle/buzzer` | `ON` / `OFF` | Liga/desliga buzzer remotamente |
| `estacao/controle/led` | `VERDE` / `AMARELO` / `VERMELHO` / `OFF` | Controla LED RGB remotamente |

---

## Lógica dos Atuadores

| Qualidade do Ar | PM2.5 | CO₂eq | LED RGB | Buzzer |
|----------------|-------|--------|---------|--------|
| BOM | ≤ 15 µg/m³ | ≤ 800 ppm | 🟢 Verde | Desligado |
| MODERADO | ≤ 35 µg/m³ | ≤ 1500 ppm | 🟡 Amarelo | Desligado |
| RUIM | > 35 µg/m³ | > 1500 ppm | 🔴 Vermelho | Bipe 2kHz |

---

## Simulação (Wokwi)

O projeto pode ser simulado em [wokwi.com](https://wokwi.com).  
Substitua o PMS5003 por leituras simuladas (já implementado no código: bloco `if (!lerPMS5003(...))`) e o MQ-135 por potenciômetro (mesma interface ADC).

---

## Node-RED Dashboard

Importe o arquivo `nodered/flows.json` no Node-RED:

1. Acesse `http://localhost:1880`
2. Menu → Import → cole o conteúdo de `nodered/flows.json`
3. Deploy

O dashboard ficará disponível em `http://localhost:1880/ui`.

---

## Estrutura do Repositório

```
micro-estacao-meteorologica/
├── firmware/
│   └── micro_estacao.ino      # Código-fonte ESP32
├── nodered/
│   └── flows.json             # Flow Node-RED (dashboard MQTT)
├── docs/
│   ├── diagrama_montagem.png  # Diagrama de conexões
│   └── fluxograma.png         # Fluxograma do firmware
└── README.md
```

---

## Licença

MIT License — livre para reprodução acadêmica com citação da fonte.
