# Zabbix sensor: Arduino + DS18B20 + Ethernet

Прошивка для **Arduino Nano/Uno** (ATmega328P) с сетевым модулем **ENC28J60** и датчиками температуры **DS18B20**. Устройство работает как **пассивный Zabbix-агент** на порту **TCP 10050**: Zabbix сам подключается и забирает данные.

Проверено на **Zabbix 7.4.8**.

**Репозиторий:** https://github.com/xxxproms/zabbix_sensor

---

## Что умеет

- Отдаёт температуру с **2 датчиков** DS18B20 по ключам `env.temp` и `env.temp1`.
- Отвечает на `agent.ping` (проверка доступности).
- Автоматически переинициализирует ENC28J60 каждые 3 минуты для стабильной работы 24/7.
- Аппаратный Watchdog Timer — если код завис, Arduino перезагрузится сама.

---

## Что понадобится

| Компонент | Примечание |
|-----------|------------|
| Arduino Nano или Uno (ATmega328P) | Контроллер |
| NANO Ethernet Shield (ENC28J60) или отдельный модуль ENC28J60 | Подключается как «бутерброд» или проводами по SPI |
| DS18B20 (1–2 шт.) | Датчики температуры, шина 1-Wire на пин D4 |
| Резистор 4.7 кОм | Подтяжка линии DQ (один на всю шину) |
| Кабель Ethernet | Для подключения к коммутатору |
| USB-кабель или блок питания 5V | Питание Arduino |

---

## Документация (карта проекта)

| Документ | Что внутри |
|----------|------------|
| [OBSHIY_BOM.md](firmware/OBSHIY_BOM.md) | Полный список комплектующих |
| [SCHEMA_I_SOEDINENIYA.md](firmware/SCHEMA_I_SOEDINENIYA.md) | Схемы подключения (блок-схемы, ASCII, Mermaid) |
| [MONTAZH_ENC28J60.md](firmware/MONTAZH_ENC28J60.md) | Подключение Ethernet-модуля, питание 3.3V |
| [MONTAZH_I_PAYKA_DS18B20.md](firmware/MONTAZH_I_PAYKA_DS18B20.md) | Пайка датчиков DS18B20, распиновка, ошибки |
| [NASTR_AYKA_I_OTLADKA.md](firmware/NASTR_AYKA_I_OTLADKA.md) | Настройка Arduino IDE, первый запуск, отладка |
| [ZABBIX.md](firmware/ZABBIX.md) | Пошаговое добавление в Zabbix (хост, элементы, проверка) |

---

## Скетчи (прошивки)

| Скетч | Назначение |
|-------|------------|
| [zabbix_ds18b20.ino](firmware/zabbix_ds18b20/zabbix_ds18b20.ino) | **Основная прошивка** — Zabbix-агент, статический IP, 2 датчика |
| [zabbix_test_2sensor.ino](firmware/zabbix_test_2sensor/zabbix_test_2sensor.ino) | Тестовая прошивка — то же, но через DHCP |
| [ds18b20_scan.ino](firmware/ds18b20_scan/ds18b20_scan.ino) | Вспомогательный — сканирует ROM-адреса датчиков |

---

## Быстрый старт

### 1. Подготовка железа

- Соберите Arduino + ENC28J60 + DS18B20 по [схеме подключения](firmware/SCHEMA_I_SOEDINENIYA.md).
- Подключите Ethernet-кабель к коммутатору.

### 2. Узнайте ROM-адреса датчиков

- Залейте [ds18b20_scan.ino](firmware/ds18b20_scan/ds18b20_scan.ino).
- Откройте Serial Monitor (9600 бод).
- Скопируйте строки `DeviceAddress tempSensorX = {...}`.

### 3. Настройте основной скетч

Откройте [zabbix_ds18b20.ino](firmware/zabbix_ds18b20/zabbix_ds18b20.ino) и отредактируйте:

- **MAC-адрес** (`mac[]`) — уникальный для каждой платы.
- **IP-адрес** (`ip`) — свободный адрес в вашей сети.
- **Шлюз и маска** (`gateway`, `subnet`) — под вашу сеть.
- **ROM датчиков** (`tempSensor1`, `tempSensor2`) — вставьте свои из сканера.

### 4. Залейте прошивку

- Arduino IDE → выберите плату (Nano / Uno), порт.
- Установите библиотеки: **OneWire**, **DallasTemperature**, **UIPEthernet**.
- Загрузите скетч.

### 5. Проверьте

- Serial Monitor (9600) покажет IP, шлюз, количество датчиков, свободную RAM.
- С сервера Zabbix: `zabbix_get -s <IP> -p 10050 -k agent.ping` → ответ `1`.

### 6. Добавьте в Zabbix

Пошаговая инструкция: [ZABBIX.md](firmware/ZABBIX.md).

---

## Стабильность (Watchdog + reinit)

ENC28J60 склонен к зависанию при слабом питании 3.3V. Прошивка защищена:

- **Watchdog Timer 8 сек** — если код завис, MCU перезагрузится автоматически.
- **Переинициализация ENC28J60 каждые 3 минуты** — не даёт чипу зависнуть.
- **Ранний сброс WDT в секции .init3** — работает с любым загрузчиком (старым и новым).

Если ENC28J60 всё равно нестабилен — рекомендуется внешний стабилизатор 3.3V (AMS1117-3.3) или замена на модуль **W5500**.

---

## Лицензия

[MIT](LICENSE)
