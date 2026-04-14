# Настройка, первый запуск и отладка

Пошаговая инструкция: от подключённых проводов до работающего мониторинга в Zabbix.

---

## Шаг 1. Соберите железо

1. Подключите Arduino + ENC28J60 + DS18B20 по [схеме](SCHEMA_I_SOEDINENIYA.md).
2. Убедитесь: **ENC28J60 на 3.3V** (не 5V!), **общий GND** везде.
3. Воткните Ethernet-кабель в коммутатор.
4. Подключите Arduino к ПК по USB.

---

## Шаг 2. Установите Arduino IDE

1. Скачайте и установите [Arduino IDE 2](https://www.arduino.cc/en/software).
2. Установите библиотеки (Sketch → Include Library → Manage Libraries):
   - **OneWire**
   - **DallasTemperature**
   - **UIPEthernet**
3. Выберите плату: **Tools → Board → Arduino Nano** (или Uno).
4. Выберите процессор: **ATmega328P**. Если загрузка не идёт — попробуйте **ATmega328P (Old Bootloader)**.
5. Выберите порт: **Tools → Port** → ваш COM/USB порт.

---

## Шаг 3. Узнайте ROM-адреса датчиков

1. Откройте скетч [ds18b20_scan/ds18b20_scan.ino](ds18b20_scan/ds18b20_scan.ino).
2. Загрузите в Arduino (кнопка Upload).
3. Откройте **Serial Monitor** (Tools → Serial Monitor, скорость **9600**).
4. Вы увидите:
   ```
   DS18B20 on bus (1-Wire): 2
   sensor 0: DeviceAddress tempSensorX = {0x28, 0x61, ...};
   sensor 1: DeviceAddress tempSensorX = {0x28, 0xB7, ...};
   ```
5. **Скопируйте** эти строки — они понадобятся на следующем шаге.

**Если `DS18B20 on bus: 0`** — датчики не найдены. Проверьте:
- Провод DQ подключён к пину **D4**?
- Есть резистор **4.7 кОм** между DQ и 5V?
- GND и VDD не перепутаны?

---

## Шаг 4. Настройте основной скетч

Откройте [zabbix_ds18b20/zabbix_ds18b20.ino](zabbix_ds18b20/zabbix_ds18b20.ino) и измените:

### 4.1. MAC-адрес

```c
static byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
```

Если у вас несколько плат — у каждой должен быть свой MAC (поменяйте последний байт).

### 4.2. IP-адрес

```c
static IPAddress ip(10, 10, 0, 50);
static IPAddress dnsServer(10, 10, 0, 1);
static IPAddress gateway(10, 10, 0, 1);
static IPAddress subnet(255, 255, 254, 0);
```

Подставьте свободный IP из вашей сети, правильный шлюз и маску.

Если хотите DHCP — закомментируйте строку `#define USE_STATIC_IP 1`:
```c
// #define USE_STATIC_IP 1
```

### 4.3. ROM-адреса датчиков

Вставьте адреса из шага 3:
```c
DeviceAddress tempSensor1 = {0x28, 0x61, 0xE7, 0x58, 0xD4, 0xE1, 0x3C, 0x8E};
DeviceAddress tempSensor2 = {0x28, 0xB7, 0x2A, 0x58, 0xD4, 0xE1, 0x3C, 0x36};
```

---

## Шаг 5. Загрузите прошивку

1. Нажмите **Upload** в Arduino IDE.
2. Дождитесь «Done uploading».
3. Откройте **Serial Monitor** (9600 бод).

Должно появиться:
```
Zabbix DS18B20 v5
  IP:  10.10.0.50
  GW:  10.10.0.1
  DS:  2
  RAM: 544
OK
```

**Если `DS: 0`** — ROM-адреса в скетче не совпадают с реальными датчиками. Вернитесь к шагу 3.

**Если `IP: 0.0.0.0`** — проблема с ENC28J60 или кабелем.

---

## Шаг 6. Проверьте с сервера

С машины, которая будет опрашивать Arduino (Zabbix-сервер или прокси):

```bash
zabbix_get -s 10.10.0.50 -p 10050 -k agent.ping
```

Ожидаемый ответ: `1`

```bash
zabbix_get -s 10.10.0.50 -p 10050 -k env.temp
```

Ожидаемый ответ: число вроде `23.50`

Если не отвечает — проверьте:
- Arduino и сервер в одной подсети (или есть маршрут)?
- Порт 10050 не заблокирован файрволом?
- Кабель подключён, линк есть?

---

## Шаг 7. Добавьте в Zabbix

Подробная пошаговая инструкция: [ZABBIX.md](ZABBIX.md).

---

## Типичные проблемы

| Симптом | Решение |
|---------|---------|
| `DS: 0` — датчики не найдены | Проверьте D4, 5V, GND, резистор 4.7 кОм |
| Нет линка Ethernet | Кабель, порт коммутатора, питание 3.3V |
| Порт 10050 не отвечает | Прошивка не та, IP неверный, файрвол |
| Зависает через N минут | ENC28J60 перегружает 3.3V регулятор. Прошивка v5 переинициализирует чип каждые 3 минуты. Если всё равно зависает — нужен внешний стабилизатор AMS1117-3.3 |
| Каждые 3 минуты в Serial `[reinit]` | Это нормально — плановая переинициализация ENC28J60 |
| `avrdude: bad CPU type` при загрузке | Обновите Arduino AVR Boards в менеджере плат |
| Не загружается в Nano | Попробуйте **Old Bootloader** в настройках процессора |

---

## Связанные документы

- [ZABBIX.md](ZABBIX.md) — настройка Zabbix
- [SCHEMA_I_SOEDINENIYA.md](SCHEMA_I_SOEDINENIYA.md) — схемы подключения
- [MONTAZH_ENC28J60.md](MONTAZH_ENC28J60.md) — подключение Ethernet
- [MONTAZH_I_PAYKA_DS18B20.md](MONTAZH_I_PAYKA_DS18B20.md) — пайка датчиков
- [OBSHIY_BOM.md](OBSHIY_BOM.md) — список комплектующих
