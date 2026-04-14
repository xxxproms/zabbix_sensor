/*
 * Zabbix passive agent (TCP 10050) + DS18B20 (DallasTemperature).
 * Стабильная работа 24/7: Watchdog, периодический сброс ENC28J60.
 *
 * Документация: ../ZABBIX.md, ../NASTR_AYKA_I_OTLADKA.md
 */

#include <string.h>
#include <avr/wdt.h>
#include <OneWire.h>
#include <UIPEthernet.h>
#include <DallasTemperature.h>

// ─── Сеть ────────────────────────────────────────────────────
static byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

#define USE_STATIC_IP 1
#if defined(USE_STATIC_IP) && USE_STATIC_IP
static IPAddress ip(10, 10, 0, 50);
static IPAddress dnsServer(10, 10, 0, 1);
static IPAddress gateway(10, 10, 0, 1);
static IPAddress subnet(255, 255, 254, 0);
#endif

// ─── Параметры ──────────────────────────────────────────────
#define ZBX_AGENT_PORT       10050
#define ZBX_HEADER_LEN       13
#define ZBX_PAYLOAD_OFFSET   ZBX_HEADER_LEN
#define ZBX_RX_BUF_SZ        160
#define CLIENT_TIMEOUT_MS    3000
#define DS18B20_PIN          4

// Полный переинит Ethernet каждые ETH_REINIT_MS (по умолчанию 30 минут).
// ENC28J60 склонен к зависанию — это главная защита.
#define ETH_REINIT_MS        1800000UL

OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);
EthernetServer server(ZBX_AGENT_PORT);

static const uint8_t kDsResolution = 10;

// ROM-адреса DS18B20 (получены сканером ds18b20_scan).
DeviceAddress tempSensor1 = {0x28, 0x61, 0xE7, 0x58, 0xD4, 0xE1, 0x3C, 0x8E};
DeviceAddress tempSensor2 = {0x28, 0xB7, 0x2A, 0x58, 0xD4, 0xE1, 0x3C, 0x36};

static unsigned long lastEthReinit;
static unsigned long requestCount;

// ─── Zabbix payload helpers ─────────────────────────────────
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
  const char msg[] = "ZBX_NOTSUPPORTED";
  client.print(F("ZBXD\x01"));
  writeLeU64(client, (uint64_t)strlen(msg));
  client.write((const uint8_t *)msg, strlen(msg));
}

// ─── Температура ────────────────────────────────────────────
static void readTemperature(EthernetClient &client, DeviceAddress addr) {
  sensors.requestTemperaturesByAddress(addr);
  float c = sensors.getTempC(addr);
  if (c == DEVICE_DISCONNECTED_C) {
    sendZbxNotSupported(client);
    return;
  }
  char buf[20];
  dtostrf(c, 1, 2, buf);
  sendZabbixPayload(client, buf);
}

// ─── Протокол ───────────────────────────────────────────────
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

// ─── Ethernet init / reinit ─────────────────────────────────
static void initEthernet() {
#if defined(USE_STATIC_IP) && USE_STATIC_IP
  Ethernet.begin(mac, ip, dnsServer, gateway, subnet);
#else
  if (Ethernet.begin(mac) == 0) {
    Serial.println(F("DHCP FAILED"));
    delay(5000);
    wdt_enable(WDTO_250MS);
    while (true) {}
  }
#endif
  server.begin();
  lastEthReinit = millis();
}

static void printNetInfo() {
  Serial.print(F("  IP:   ")); Serial.println(Ethernet.localIP());
  Serial.print(F("  Mask: ")); Serial.println(Ethernet.subnetMask());
  Serial.print(F("  GW:   ")); Serial.println(Ethernet.gatewayIP());
  Serial.print(F("  DNS:  ")); Serial.println(Ethernet.dnsServerIP());
}

// ─── Setup ──────────────────────────────────────────────────
void setup() {
  wdt_disable();
  delay(500);
  Serial.begin(9600);
  Serial.println(F("Zabbix DS18B20 agent v2 (watchdog + reinit)"));

  initEthernet();

  sensors.begin();
  sensors.setWaitForConversion(true);
  sensors.setResolution(kDsResolution);

  printNetInfo();
  Serial.print(F("DS18B20 count: "));
  Serial.println(sensors.getDeviceCount(), DEC);
  Serial.println(F("Ready. WDT 8s enabled."));

  requestCount = 0;
  wdt_enable(WDTO_8S);
}

// ─── Loop ───────────────────────────────────────────────────
void loop() {
  wdt_reset();

  // Превентивный сброс ENC28J60 каждые ETH_REINIT_MS
  if ((millis() - lastEthReinit) > ETH_REINIT_MS) {
    Serial.print(F("[reinit eth] uptime="));
    Serial.print(millis() / 60000UL);
    Serial.print(F("m reqs="));
    Serial.println(requestCount);
    initEthernet();
    printNetInfo();
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
