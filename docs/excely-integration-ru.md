# Интеграция Excely (SmartHotel)

Краткое руководство по настройке PMSConnect в программе.

Есть **две разные вещи**: импорт броней и выгрузка доступности. Карта типов (`roomTypeMap`) нужна только для импорта.

---

## 1. Подключение (обязательно для всего)

**Application → Global config → вкладка Application → блок Excely**

Заполните и сохраните:

| Поле | Что писать |
|------|------------|
| Endpoint | тест: `https://pmsconnect.test.hopenapi.com/Api/PMSConnect.svc` |
| Hotel code | код отеля в Exely |
| Username / Password | логин API |
| Protocol | `1.18` |

Без этого ни кнопка на шахматке, ни форма доступности работать не будут.

Прод-endpoint: `https://pmsconnect.hopenapi.com/Api/PMSConnect.svc`.

Настройки пишутся в QSettings `SmartHotel` / `SmartHotel`, группа `excely/`.

---

## 2. Выгрузка доступности (Excely availability)

Форма **не** берёт категории из вашей БД. Типы и тарифы приходят **из Exely**.

1. **Directory of Hotel → Excely availability**  
   (нужно право *Excely availability* / `cr__excely_availability` = 153 в User rights).
2. Нажмите **Refresh catalog**.
3. После успеха в логе будет что-то вроде: `Catalog: N room types, M rate plans`.
4. Тогда заполнятся **Room type**, **Rate plan** / **Inv block**.

Дальше: даты, Available rooms (`BookingLimit`), Open/Close → **Upload**  
или **Resync year** (End = Start + 365 дней).

Если после Refresh список пустой — в Exely для этого hotel code нет каталога, либо ошибка логина (смотрите текст ошибки / лог формы).

Протокол: `OTA_HotelAvailNotifRQ` (SOAPAction `…/HotelAvailNotifRQ`).

---

## 3. Карта типов (`roomTypeMap`) — только для импорта броней

Нужна, когда нажимаете **Excely** на шахматке: бронь из канала должна попасть в вашу категорию и свободный номер.

В том же блоке Excely в Global config поле **Room type map (JSON)**:

```json
{ "5003606": "STD", "5003607": "DLX" }
```

- слева — **код типа в Exely** (тот же, что в каталоге после Refresh);
- справа — **`f_short` вашей категории** из Directory (Categories) или числовой `f_id` класса.

Без карты бронь создаётся, но **`f_room = 0`** — на шахматке не видно, пока номер не назначат вручную.

---

## 4. Импорт броней (кнопка Excely на шахматке)

1. Настроить credentials и по возможности `roomTypeMap` → Save.
2. На шахматке нажать **Excely**.
3. Программа: pull undelivered → запись в `f_reservation` (`f_chm`, `f_chmstatus=1`) → confirm.
4. Новые брони рисуются **сиреневым**, пока оператор не откроет/сохранит резерв (`f_chmstatus` → 2).

Лимиты Exely: ≤30 ReadRQ/час; подтверждение roughly в течение ~20 минут.

---

## 5. Рабочий порядок

1. Настроить credentials → Save.  
2. Availability → **Refresh catalog** → выставить квоты → Upload.  
3. Заполнить `roomTypeMap` по кодам из каталога.  
4. На шахматке кнопка **Excely** — забрать новые брони.

---

## Где что в коде / UI

| Где | Что |
|-----|-----|
| Desk кнопка **Excely** | Импорт undelivered броней |
| **Directory of Hotel → Excely availability** | Ручная выгрузка доступности |
| **Global config → Application → Excely** | Credentials + `roomTypeMap` |
| `excely/README.md` | Техническое описание модуля PMSConnect |

### Ключи QSettings (`excely/`)

| Key | Назначение |
|-----|------------|
| `endpoint` | URL PMSConnect |
| `username` / `password` | API |
| `hotelCode` | Код отеля |
| `protocolVersion` | по умолчанию `1.18` |
| `roomTypeMap` | JSON: код CM → `f_short` / `f_id` класса |

Секреты в репозиторий не коммитить.
