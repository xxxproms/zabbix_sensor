# Zabbix sensor: Arduino + DS18B20 + пассивный Zabbix agent

Прошивка для **Arduino** с **Ethernet (ENC28J60 + UIPEthernet)** и датчиками **DS18B20** по шине **1-Wire**. Устройство отвечает на запросы **Zabbix Server / Proxy** как **пассивный Zabbix agent** на **TCP 10050** и отдаёт значения температуры по заранее заданным ключам.

Цель проекта — недорогой съём температуры в стойке, серверной или бытовых зонах с прямой интеграцией в Zabbix **без промежуточных скриптов** на стороне сервера (только стандартный тип элемента «Zabbix agent»).

**Проверка на стороне сервера:** Zabbix **7.4.8** (пассивный опрос по тому же протоколу, что у Zabbix agent; см. [firmware/ZABBIX.md](firmware/ZABBIX.md)).

### Документация проекта (карта)

| Документ | Содержание |
|----------|------------|
| [firmware/OBSHIY_BOM.md](firmware/OBSHIY_BOM.md) | Общий список комплектующих на одно устройство |
| [firmware/SCHEMA_I_SOEDINENIYA.md](firmware/SCHEMA_I_SOEDINENIYA.md) | Блок-схемы (Mermaid), ASCII-схемы 1-Wire и ENC28J60 |
| [firmware/MONTAZH_ENC28J60.md](firmware/MONTAZH_ENC28J60.md) | Подключение Ethernet-модуля, питание 3.3 В, таблица пинов |
| [firmware/MONTAZH_I_PAYKA_DS18B20.md](firmware/MONTAZH_I_PAYKA_DS18B20.md) | Пайка DS18B20, BOM, типичные ошибки |
| [firmware/NASTR_AYKA_I_OTLADKA.md](firmware/NASTR_AYKA_I_OTLADKA.md) | Настройка IDE, ROM датчиков, Zabbix, отладка |
| [firmware/ZABBIX.md](firmware/ZABBIX.md) | Ключи агента, элементы данных, протокол TCP |
| [firmware/ds18b20_scan/](firmware/ds18b20_scan/) | Вспомогательный скетч для чтения адресов DS18B20 |

---

## Возможности

- Пассивный опрос Zabbix (тот же порт и бинарный заголовок `ZBXD`, что у классического агента).
- До **четырёх** датчиков DS18B20 на одной линии 1-Wire (адреса ROM задаются в скетче).
- Ключи: `agent.ping`, `env.temp`, `env.temp1` … `env.temp3`; неизвестный ключ → ответ с телом `ZBX_NOTSUPPORTED` и корректной длиной в заголовке.
- Чтение температуры через **DallasTemperature** (разрешение по умолчанию **10 бит**, ожидание преобразования включено).
- Приём запроса буфером фиксированного размера, разбор **полной** длины payload (**uint64 little-endian**), без класса `String` в горячем пути (меньше фрагментации кучи на AVR).

Подробнее по ключам, элементам данных и проверке протокола — **[firmware/ZABBIX.md](firmware/ZABBIX.md)**.

**Пайка и распайка DS18B20 (пины, BOM, типичные ошибки):** **[firmware/MONTAZH_I_PAYKA_DS18B20.md](firmware/MONTAZH_I_PAYKA_DS18B20.md)**.

---

## Аппаратная часть

| Компонент | Примечание |
|-----------|------------|
| Arduino-совместимая плата | Например Uno/Nano (ATmega328P) |
| Модуль Ethernet **ENC28J60** | Стек **UIPEthernet** |
| DS18B20 | 1–4 шт., линия данных на **D4** (константа `DS18B20_PIN` в скетче) |
| Питание и подтяжка 1-Wire | Резистор 4.7 kΩ к VCC на DQ при схеме с внешним питанием датчиков |

У **ENC28J60** обеспечьте **стабильное 3.3 В**; при проблемах с сетью часто помогает **статический IP** (см. `USE_STATIC_IP` в скетче).

---

## Программная часть (Arduino IDE / Arduino CLI)

### Расположение скетчей

- Основной проект: [firmware/zabbix_ds18b20/zabbix_ds18b20.ino](firmware/zabbix_ds18b20/zabbix_ds18b20.ino)
- Поиск адресов DS18B20: [firmware/ds18b20_scan/ds18b20_scan.ino](firmware/ds18b20_scan/ds18b20_scan.ino)

Откройте папку со скетчем в Arduino IDE (имя папки должно совпадать с именем `.ino`).

### Зависимости (менеджер библиотек)

- **OneWire**
- **DallasTemperature** (Miles Burton / актуальний форк для вашей платы)
- **UIPEthernet** (для ENC28J60)

### Что настроить перед прошивкой

1. **MAC-адрес** массив `mac[]` — уникальный в вашей L2-сети.
2. **Статический IP под вашу сеть:** в скетче по умолчанию **10.10.0.0/23** (`255.255.254.0`), хосты **10.10.0.30–10.10.1.254** — задайте уникальный `ip` на каждую плату, при необходимости **gateway** и **dnsServer** (по умолчанию в примере `10.10.0.1`). Закомментируйте `#define USE_STATIC_IP`, если нужен только DHCP.
3. **ROM-адреса** DS18B20 в `tempSensor1` … `tempSensor4` — подставьте свои (например, выведите через пример «sensor search» DallasTemperature).

---

## Интеграция с Zabbix

Полная пошаговая инструкция для **Zabbix 7.4.x** (хост, группа, интерфейс Agent, элементы данных, прокси, проверка `zabbix_get`, **Last data**) и технический разбор протокола — в **[firmware/ZABBIX.md](firmware/ZABBIX.md)**.

---

## Откуда взялась прошивка и что улучшено

Исходная идея — самописный агент под старый код с `String`, неинициализированным `addr` для MAC/EEPROM DS18B20, короткой задержкой преобразования и неполным разбором длины запроса. Текущая версия в репозитории исправляет эти моменты и опирается на типичную реализацию заголовка Zabbix (в духе проектов вроде [zabbuino](https://github.com/zbx-sadman/zabbuino)).

---

## Похожие открытые проекты

- [zabbuino](https://github.com/zbx-sadman/zabbuino) — расширяемый Zabbix agent для Arduino с множеством датчиков.
- [arduino-zabbix-temp](https://github.com/hedgeven/arduino-zabbix-temp) — температуры и Zabbix.
- [arduino-zabbix-agent](https://github.com/marcofischer/arduino-zabbix-agent), [Arduino-Zabbix-Agent](https://github.com/interlegis/Arduino-Zabbix-Agent) — агенты на Arduino.

---

## Лицензия

[MIT](LICENSE)

---

## Публикация на GitHub

Основной репозиторий проекта: **https://github.com/xxxproms/zabbix_sensor**

Если вы клонируете проект и хотите выложить **свою** копию, создайте репозиторий **в своём аккаунте** (веб-интерфейс или GitHub CLI), затем привяжите `remote` и отправьте ветку:

```bash
cd /path/to/zabbix_sensor
git remote add origin https://github.com/<ваш_user>/<имя_репо>.git
git branch -M main
git push -u origin main
```

С **GitHub CLI** (`gh auth login` уже выполнен):

```bash
gh repo create <имя_репо> --public --source=. --remote=origin --push
```

Рекомендуется в настройках репозитория на GitHub добавить **описание** и темы (*topics*), например: `zabbix`, `arduino`, `ds18b20`, `enc28j60`, `monitoring`.
