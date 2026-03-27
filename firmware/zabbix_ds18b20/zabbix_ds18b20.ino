/*
 * Zabbix passive agent (TCP 10050) + DS18B20 (DallasTemperature).
 *
 * Ключи и настройка в Zabbix: ../ZABBIX.md
 * Смещение ключа во входящем кадре — ZBX_PAYLOAD_OFFSET (сверка с дампом): ../ZABBIX.md
 * Пайка DS18B20, пины, комплектующие: ../MONTAZH_I_PAYKA_DS18B20.md
 */

#include <string.h>
#include <OneWire.h>
#include <UIPEthernet.h>
#include <DallasTemperature.h>

// Уникальный MAC на каждой плате в L2 (не дублируйте на разных Ардуино).
static byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

// Сеть 10.10.0.0/23: маска 255.255.254.0, допустимые хосты 10.10.0.30–10.10.1.254.
// На каждое устройство — свой ip и свой mac[].
// Закомментируйте USE_STATIC_IP, если нужен только DHCP.
#define USE_STATIC_IP 1
#if defined(USE_STATIC_IP) && USE_STATIC_IP
static IPAddress ip(10, 10, 0, 50);
static IPAddress dnsServer(10, 10, 0, 1);
static IPAddress gateway(10, 10, 0, 1);
static IPAddress subnet(255, 255, 254, 0);
#endif

#define ZBX_AGENT_PORT       10050
#define ZBX_HEADER_LEN       13
#define ZBX_PAYLOAD_OFFSET   ZBX_HEADER_LEN
#define ZBX_RX_BUF_SZ        160

#define DS18B20_PIN          4

OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);
EthernetServer server(ZBX_AGENT_PORT);

// 9..12 бит; библиотека выдержит время преобразования при setWaitForConversion(true).
static const uint8_t kDsResolution = 10;

DeviceAddress tempSensor1 = {0x28, 0x2C, 0x68, 0x58, 0xD4, 0xE1, 0x3C, 0x15};
DeviceAddress tempSensor2 = {0x28, 0xCB, 0x04, 0x43, 0xD4, 0xE1, 0x3C, 0x25};
DeviceAddress tempSensor3 = {0x28, 0x97, 0x95, 0x43, 0xD4, 0xE1, 0x3C, 0x0C};
DeviceAddress tempSensor4 = {0x28, 0xB7, 0x7A, 0x43, 0xD4, 0xE1, 0x3C, 0x0F};

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
  } else if (strcmp(key, "env.temp2") == 0) {
    readTemperature(client, tempSensor3);
  } else if (strcmp(key, "env.temp3") == 0) {
    readTemperature(client, tempSensor4);
  } else {
    sendZbxNotSupported(client);
  }
}

void setup() {
  delay(500);
#if defined(USE_STATIC_IP) && USE_STATIC_IP
  Ethernet.begin(mac, ip, dnsServer, gateway, subnet);
#else
  Ethernet.begin(mac);
#endif
  server.begin();

  sensors.begin();
  sensors.setWaitForConversion(true);
  sensors.setResolution(kDsResolution);

  Serial.begin(9600);
  Serial.print(F("DS18B20 count: "));
  Serial.println(sensors.getDeviceCount(), DEC);
}

void loop() {
  static char rxBuf[ZBX_RX_BUF_SZ];
  EthernetClient client = server.available();
  if (!client) {
    return;
  }

  size_t n = 0;
  bool oversize = false;

  while (client.connected()) {
    if (client.available()) {
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
