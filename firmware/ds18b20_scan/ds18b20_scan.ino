/*
 * Вспомогательный скетч: вывод ROM-адресов всех DS18B20 на шине 1-Wire.
 * Пин данных должен совпадать с основным проектом (D4).
 *
 * Сеть (Ethernet) здесь не используется — IP в мониторе не появится.
 * Адрес по DHCP/статика смотрите в zabbix_ds18b20.ino или zabbix_test_2sensor.ino
 * (ENC28J60 подключён, кабель в сеть).
 *
 * После запуска откройте Serial Monitor (9600), скопируйте строки в
 * DeviceAddress tempSensor1 ... tempSensor4 в zabbix_ds18b20.ino
 */

#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 4

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

void setup() {
  Serial.begin(9600);
  delay(300);

  sensors.begin();
  uint8_t n = sensors.getDeviceCount();
  Serial.print(F("DS18B20 on bus (1-Wire): "));
  Serial.println(n, DEC);
  Serial.println(F("--- Copy to DeviceAddress in zabbix_ds18b20.ino ---"));

  for (uint8_t i = 0; i < n; i++) {
    DeviceAddress addr;
    if (sensors.getAddress(addr, i)) {
      Serial.print(F("sensor "));
      Serial.print(i, DEC);
      Serial.print(F(": DeviceAddress tempSensorX = {"));
      for (uint8_t j = 0; j < 8; j++) {
        Serial.print(F("0x"));
        if (addr[j] < 16) {
          Serial.print('0');
        }
        Serial.print(addr[j], HEX);
        if (j < 7) {
          Serial.print(F(", "));
        }
      }
      Serial.println(F("};"));
    }
  }
  Serial.println(F("--- end ---"));
  Serial.println();
  Serial.println(F("IP здесь не показывается: этот скетч без Ethernet."));
  Serial.println(F("Залейте zabbix_test_2sensor или zabbix_ds18b20 — там в мониторе будет IP/GW/DNS."));
}

void loop() {
}
