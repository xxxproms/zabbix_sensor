/*
 * Тестовая плата: Zabbix agent (10050) + 2x DS18B20.
 * Сеть: DHCP (Ethernet.begin(mac)). В Zabbix укажите IP из Serial или резерв на роутере по MAC.
 * ROM сняты сканером.
 *
 * Ключи: agent.ping, env.temp (сенсор 0), env.temp1 (сенсор 1).
 *
 * Документация проекта: ../ZABBIX.md, ../NASTR_AYKA_I_OTLADKA.md
 */

#include <string.h>
#include <OneWire.h>
#include <UIPEthernet.h>
#include <DallasTemperature.h>

// Уникальный MAC этой тестовой платы (не дублируйте с другими устройствами в L2).
static byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xEE};

#define ZBX_AGENT_PORT       10050
#define ZBX_HEADER_LEN       13
#define ZBX_PAYLOAD_OFFSET   ZBX_HEADER_LEN
#define ZBX_RX_BUF_SZ        160

#define CLIENT_TIMEOUT_MS    3000

#define DS18B20_PIN          4

OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);
EthernetServer server(ZBX_AGENT_PORT);

static const uint8_t kDsResolution = 10;

DeviceAddress tempSensor1 = {0x28, 0x61, 0xE7, 0x58, 0xD4, 0xE1, 0x3C, 0x8E};
DeviceAddress tempSensor2 = {0x28, 0xB7, 0x2A, 0x58, 0xD4, 0xE1, 0x3C, 0x36};

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

static void readTemperature(EthernetClient &client, DeviceAddress deviceAddress) {
  sensors.requestTemperaturesByAddress(deviceAddress);
  float c = sensors.getTempC(deviceAddress);
  if (c == DEVICE_DISCONNECTED_C) {
    sendZbxNotSupported(client);
    return;
  }
  char buf[20];
  dtostrf(c, 1, 2, buf);
  sendZabbixPayload(client, buf);
}

static bool decodePayloadLen(const char *hdr, uint64_t *outLen) {
  uint64_t payloadLen = 0;
  for (uint8_t i = 0; i < 8; i++) {
    payloadLen |= (uint64_t)(uint8_t)hdr[5 + i] << (i * 8);
  }
  *outLen = payloadLen;
  if (payloadLen > (uint64_t)(ZBX_RX_BUF_SZ - ZBX_HEADER_LEN - 1)) {
    return false;
  }
  return true;
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

void setup() {
  delay(500);
  Serial.begin(9600);
  Serial.println(F("TEST 2x DS18B20, DHCP..."));

  if (Ethernet.begin(mac) == 0) {
    Serial.println(F("DHCP failed — проверьте кабель и роутер"));
    while (true) {
      delay(1000);
    }
  }

  server.begin();

  sensors.begin();
  sensors.setWaitForConversion(true);
  sensors.setResolution(kDsResolution);

  Serial.println(F("DHCP OK, сеть:"));
  Serial.print(F("  IP:   "));
  Serial.println(Ethernet.localIP());
  Serial.print(F("  Mask: "));
  Serial.println(Ethernet.subnetMask());
  Serial.print(F("  GW:   "));
  Serial.println(Ethernet.gatewayIP());
  Serial.print(F("  DNS:  "));
  Serial.println(Ethernet.dnsServerIP());
  Serial.print(F("DS18B20 count: "));
  Serial.println(sensors.getDeviceCount(), DEC);
}

void loop() {
  (void)Ethernet.maintain();

  static char rxBuf[ZBX_RX_BUF_SZ];
  EthernetClient client = server.available();
  if (!client) {
    return;
  }

  size_t n = 0;
  bool oversize = false;
  unsigned long t0 = millis();

  while (client.connected()) {
    if ((millis() - t0) > CLIENT_TIMEOUT_MS) {
      break;
    }
    if (client.available()) {
      t0 = millis();
      if (n >= ZBX_RX_BUF_SZ - 1) {
        while (client.available()) {
          (void)client.read();
        }
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
