/*
 * Тестовая плата: Zabbix agent (10050) + 2x DS18B20, DHCP.
 * Стабильная работа 24/7: Watchdog + аппаратный сброс ENC28J60.
 *
 * ВАЖНО: подключите провод Arduino D3 → RST на ENC28J60.
 *
 * Ключи: agent.ping, env.temp, env.temp1.
 * Документация: ../ZABBIX.md, ../NASTR_AYKA_I_OTLADKA.md
 */

#include <string.h>
#include <avr/wdt.h>
#include <OneWire.h>
#include <UIPEthernet.h>
#include <DallasTemperature.h>

void wdt_early_disable(void) __attribute__((naked, used, section(".init3")));
void wdt_early_disable(void) {
  MCUSR = 0;
  wdt_disable();
}

static byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xEE};

#define ZBX_AGENT_PORT       10050
#define ZBX_HEADER_LEN       13
#define ZBX_PAYLOAD_OFFSET   ZBX_HEADER_LEN
#define ZBX_RX_BUF_SZ        128
#define CLIENT_TIMEOUT_MS    3000
#define DS18B20_PIN          4
#define ENC_RESET_PIN        3
#define ETH_REINIT_MS        300000UL

OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);
EthernetServer server(ZBX_AGENT_PORT);

static const uint8_t kDsResolution = 10;

DeviceAddress tempSensor1 = {0x28, 0x61, 0xE7, 0x58, 0xD4, 0xE1, 0x3C, 0x8E};
DeviceAddress tempSensor2 = {0x28, 0xB7, 0x2A, 0x58, 0xD4, 0xE1, 0x3C, 0x36};

static unsigned long lastEthReinit;
static unsigned long requestCount;

static void hardResetENC() {
#ifdef ENC_RESET_PIN
  pinMode(ENC_RESET_PIN, OUTPUT);
  digitalWrite(ENC_RESET_PIN, LOW);
  delay(50);
  digitalWrite(ENC_RESET_PIN, HIGH);
  delay(200);
#endif
}

static void writeLeU64(EthernetClient &client, uint64_t v) {
  uint8_t b[8];
  for (uint8_t i = 0; i < 8; i++) {
    b[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
  }
  client.write(b, 8);
}

static void sendZabbixPayload(EthernetClient &client, const char *payload) {
  client.print(F("ZBXD\x01"));
  writeLeU64(client, (uint64_t)strlen(payload));
  client.print(payload);
}

static void sendZbxNotSupported(EthernetClient &client) {
  sendZabbixPayload(client, "ZBX_NOTSUPPORTED");
}

static void readTemperature(EthernetClient &client, DeviceAddress addr) {
  wdt_reset();
  sensors.requestTemperaturesByAddress(addr);
  float c = sensors.getTempC(addr);
  if (c == DEVICE_DISCONNECTED_C) {
    sendZbxNotSupported(client);
    return;
  }
  char buf[16];
  dtostrf(c, 1, 2, buf);
  sendZabbixPayload(client, buf);
}

static bool decodePayloadLen(const char *hdr, uint64_t *outLen) {
  uint64_t payloadLen = 0;
  for (uint8_t i = 0; i < 8; i++) {
    payloadLen |= (uint64_t)(uint8_t)hdr[5 + i] << (i * 8);
  }
  *outLen = payloadLen;
  return payloadLen <= (uint64_t)(ZBX_RX_BUF_SZ - ZBX_HEADER_LEN - 1);
}

static void handleRequest(EthernetClient &client, char *rxBuf, size_t payloadLen) {
  rxBuf[ZBX_HEADER_LEN + payloadLen] = '\0';
  char *key = &rxBuf[ZBX_PAYLOAD_OFFSET];
  size_t kl = payloadLen;
  while (kl > 0 && (key[kl - 1] == '\n' || key[kl - 1] == '\r')) {
    key[--kl] = '\0';
  }

  if (strcmp(key, "agent.ping") == 0) {
    sendZabbixPayload(client, "1");
  } else if (strcmp(key, "env.temp") == 0) {
    readTemperature(client, tempSensor1);
  } else if (strcmp(key, "env.temp1") == 0) {
    readTemperature(client, tempSensor2);
  } else {
    sendZbxNotSupported(client);
  }
}

static void initEthernet() {
  wdt_disable();
  hardResetENC();
  if (Ethernet.begin(mac) == 0) {
    Serial.println(F("DHCP FAILED"));
    delay(5000);
    asm volatile ("jmp 0");
  }
  server.begin();
  lastEthReinit = millis();
  wdt_enable(WDTO_8S);
}

void setup() {
  delay(500);
  Serial.begin(9600);
  Serial.println(F("TEST 2xDS v4 DHCP"));

  initEthernet();

  sensors.begin();
  sensors.setWaitForConversion(true);
  sensors.setResolution(kDsResolution);

  Serial.print(F("  IP:  "));  Serial.println(Ethernet.localIP());
  Serial.print(F("  GW:  "));  Serial.println(Ethernet.gatewayIP());
  Serial.print(F("  DS:  "));  Serial.println(sensors.getDeviceCount(), DEC);

  extern int __heap_start, *__brkval;
  int v;
  Serial.print(F("  RAM: "));
  Serial.println((int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval));
  Serial.println(F("OK"));

  requestCount = 0;
}

void loop() {
  wdt_reset();
  (void)Ethernet.maintain();

  if ((millis() - lastEthReinit) > ETH_REINIT_MS) {
    Serial.print(F("[reinit] r="));
    Serial.println(requestCount);
    initEthernet();
  }

  static char rxBuf[ZBX_RX_BUF_SZ];
  EthernetClient client = server.available();
  if (!client) {
    return;
  }

  size_t n = 0;
  bool oversize = false;
  unsigned long t0 = millis();

  while (client.connected()) {
    wdt_reset();
    if ((millis() - t0) > CLIENT_TIMEOUT_MS) {
      break;
    }
    if (client.available()) {
      t0 = millis();
      if (n >= ZBX_RX_BUF_SZ - 1) {
        while (client.available()) { (void)client.read(); }
        oversize = true;
        break;
      }
      rxBuf[n++] = (char)client.read();

      if (n >= ZBX_HEADER_LEN) {
        uint64_t payloadLen = 0;
        if (!decodePayloadLen(rxBuf, &payloadLen)) {
          oversize = true;
          break;
        }
        size_t need = ZBX_HEADER_LEN + (size_t)payloadLen;
        if (need > ZBX_RX_BUF_SZ - 1) {
          oversize = true;
          break;
        }
        if (n == need) {
          handleRequest(client, rxBuf, (size_t)payloadLen);
          requestCount++;
          break;
        }
      }
    } else if (oversize) {
      break;
    } else {
      delay(1);
    }
  }

  delay(10);
  client.stop();
}
