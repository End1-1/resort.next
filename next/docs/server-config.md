# Файл `hotel-api.ini`

`hotel-api` читает настройки из `hotel-api.ini`. Переменные окружения для старта не обязательны. Имя файла именно `hotel-api.ini`: рядом лежащий `hotel-api.ini.example` процесс не открывает.

Ключи пишутся латиницей, без секции (или в секции `[General]`). Секция с другим именем прячет ключ: `QSettings` его не увидит. Строки с `#` в начале — комментарии.

Пустой файл тоже считается найденным. Тогда действуют встроенные значения: слушать `127.0.0.1:8080`, базу не открывать, WebSocket выключен.

## Куда класть файл

Поиск идёт сверху вниз. Берётся первый существующий файл. Нечитаемый файл останавливает процесс и не пропускает поиск дальше.

1. `HOTEL_CONFIG`, если переменная задана и после обрезки пробелов не пустая. Это полный путь к файлу, он заменяет шаги 2 и 3. Файла нет — процесс завершается, в логе `HOTEL_CONFIG does not exist: <путь>`.
2. `hotel-api.ini` в каталоге исполняемого файла. Это каталог `hotel-api.exe` / `hotel-api`, не текущий рабочий каталог процесса.
3. Только Linux: `/etc/hotel-api/hotel-api.ini`.
4. Ни одного файла нет. Лог: `hotel-api config none`. Слушать `127.0.0.1:8080`, база не настроена, WebSocket выключен.

В логе при успехе одна строка `hotel-api config` и абсолютный путь. В этой строке нет DSN и пароля.

### Qt Creator, Debug, Windows

Рабочий каталог запуска в Kits может быть каталогом исходников. На настройки это не влияет: читается файл рядом с `hotel-api.exe`.

Каталог exe — выход сборки, не `next/server/config/`. У MSVC это обычно папка `Debug` или `Release` внутри build. CMake копирует туда `hotel-api.ini.example` (не боевой ini). Скопируйте его в `hotel-api.ini` в ту же папку и впишите `dsn`.

Пока рядом с exe нет `hotel-api.ini` и `HOTEL_CONFIG` пуст, лог будет `hotel-api config none` и `hotel-api database not configured`. `GET /health` при этом отвечает `200` и `"db":{"configured":false,"state":"skipped"}`. Это не обрыв связи с MariaDB.

### Служба Windows

Служба `HotelApi` стартует с рабочим каталогом `System32`. Ini из `System32` не читается. Файл кладётся рядом с `hotel-api.exe`, тем же правилом, что и в отладчике.

Установка (`hotel-api --install`) регистрирует службу от **LocalSystem** (учётная запись в `CreateService` не задаётся). LocalSystem читает каталоги вроде `C:\hotel-api\`. Каталог сборки в профиле пользователя (`C:\Users\...\`) служба часто прочитать не может. Для службы держите exe, ini и `libmariadb.dll` в отдельном каталоге, доступном LocalSystem, либо той учётной записи, под которой службу перевели.

### Linux

Рядом с бинарником, например `/usr/local/bin/hotel-api.ini`, файл находится раньше, чем `/etc/hotel-api/hotel-api.ini`. Для пароля удобнее один файл в `/etc/hotel-api/` и отсутствие копии рядом с бинарником: иначе правка в `/etc` не подхватится.

Юнит `next/server/deploy/hotel-api.service` запускает `/usr/local/bin/hotel-api` от пользователя `hotel-api`. `EnvironmentFile=/etc/hotel-api/hotel-api.env` по-прежнему только для переменных; сам ini этот файл не заменяет.

## Пример

Боевой файл. Пароль сюда подставляется на сервере, в git он не коммитится.

```ini
# next/docs/server-config.md
listen=127.0.0.1:8080

# Пароль p@ss:word записан как p%40ss%3Aword
dsn=mysql://hotel_api:p%40ss%3Aword@127.0.0.1:3306/resort

# Пусто — сокет не поднимается. Порт не должен совпадать с listen.
ws_listen=
```

Тот же смысл с выключенной базой (health не ходит в MariaDB):

```ini
listen=127.0.0.1:8080
dsn=
ws_listen=
```

## Ключ `listen`

Адрес HTTP. Переменная перекрытия: `HOTEL_LISTEN`.

Если ключа в файле нет, значение `127.0.0.1:8080`. Если ключ есть и он пустой, старт обрывается: `HOTEL_LISTEN is empty`.

Формат: `<числовой IP>:<порт>` либо один порт. Один порт значит `127.0.0.1` и этот порт. Порт от 1 до 65535. Имя хоста (`localhost`) не принимается: адрес должен разобраться как числовой IP.

| Запись | Результат |
|--------|-----------|
| `127.0.0.1:8080` | петля, порт 8080 |
| `0.0.0.0:8080` | все интерфейсы IPv4, порт 8080 |
| `8080` | то же, что `127.0.0.1:8080` |
| `::1:8080` | IPv6-петля, порт берётся после последнего `:` |
| `localhost:8080` | отказ: `HOTEL_LISTEN host must be a numeric IP address` |
| `127.0.0.1` | отказ: `HOTEL_LISTEN must be <ip>:<port>, for example 127.0.0.1:8080` |
| `[::1]:8080` | отказ: скобки в адресе не разбираются |
| `127.0.0.1:0`, `127.0.0.1:70000` | отказ: `HOTEL_LISTEN has an invalid port` |

Порт, который уже занят, до HTTP не доводит. Процесс пишет `failed to bind HTTP <ip>:<port> (...)` и завершается с кодом 1. На Qt 6.4 текст короче, без причины сокета; на Qt 6.8+ (в том числе 6.10) в скобках есть текст ошибки сокета.

`0.0.0.0` открывает порт наружу. Для приёмной машины без отдельного файрвола оставляйте `127.0.0.1`.

## Ключ `dsn`

Строка подключения MariaDB. Переменная перекрытия: `HOTEL_DSN`.

Пустая строка и отсутствующий ключ значат одно: база не настроена. `GET /health` даёт `200`, `db.state=skipped`. `POST /api/v1/sessions` даёт `503` и `error` `database_not_configured`. В логе: `hotel-api database not configured`.

Формат:

```text
mysql://USER:PASSWORD@HOST:PORT/DATABASE
```

Схема только `mysql`. Порт можно не писать, тогда он `3306`. Имя базы — один сегмент пути, без второго `/`. Знаки `?` и `#` запрещены: `HOTEL_DSN must not include a query or fragment`.

Разбор обрывает процесс до открытия порта. Текст ошибки не содержит введённую строку.

| Запись | Результат |
|--------|-----------|
| `mysql://hotel_api:secret@127.0.0.1:3306/resort` | хост `127.0.0.1`, порт `3306`, база `resort` |
| `mysql://hotel_api:secret@127.0.0.1/resort` | порт по умолчанию `3306` |
| `mysql://127.0.0.1:3306/resort` | синтаксис принят, пользователь пустой; MariaDB ответит отказом уже на соединении |
| пусто | база выключена, это не ошибка разбора |
| `mariadb://user:secret@127.0.0.1/resort` | `HOTEL_DSN must be mysql://USER:PASSWORD@HOST:PORT/DATABASE` |
| `mysql://user:secret@/resort` | `HOTEL_DSN is missing a host` |
| `mysql://user:secret@127.0.0.1:3306` | `HOTEL_DSN is missing a database name` |
| `mysql://user:secret@127.0.0.1:3306/resort/extra` | `HOTEL_DSN is missing a database name` |
| `mysql://user:secret@127.0.0.1:3306/resort?charset=utf8mb4` | `HOTEL_DSN must not include a query or fragment` |
| `mysql://user:secret@127.0.0.1:99999/resort` | `HOTEL_DSN has an invalid port` |

### Знаки `@` и `:` в логине и пароле

`@` отделяет пользователя от хоста, `:` отделяет пользователя от пароля. Сырой пароль `p@ss:word` ломает разбор. Эти два знака в `USER` и `PASSWORD` записываются так:

| Символ | В DSN |
|--------|-------|
| `@` | `%40` |
| `:` | `%3A` |

Пароль `p@ss:word` в файле выглядит как `p%40ss%3Aword`:

```text
mysql://hotel_api:p%40ss%3Aword@127.0.0.1:3306/resort
```

Так же кодируются знаки, которые режут URL, если они встречаются в логине или пароле: `/` → `%2F`, `?` → `%3F`, `#` → `%23`, пробел → `%20`, сам знак процента → `%25`. Хост и имя базы пишутся как есть.

## Ключ `ws_listen`

Адрес WebSocket `/api/v1/ws`. Переменная перекрытия: `HOTEL_WS_LISTEN`.

Нет ключа, пусто или одни пробелы — сокет не слушается, в логе нет строки `hotel-api websocket`. Непустая строка разбирается теми же правилами, что `listen` (ошибки будут с именем `HOTEL_WS_LISTEN`).

Порт должен отличаться от `listen`: это два сокета. Занятый порт даёт `failed to bind WebSocket <ip>:<port> (...)` и код выхода 1.

Удачный старт:

```text
hotel-api websocket 127.0.0.1 8081 path /api/v1/ws
```

Кадр приветствия есть, событий PMS нет. Браузерный `Origin` принимается только с `127.0.0.1` и `localhost`.

## Переменные окружения

Переменная перекрывает одноимённый ключ ini только когда она задана и после обрезки пробелов не пустая.

| Переменная | Ключ | Пустое значение |
|------------|------|-----------------|
| `HOTEL_CONFIG` | путь к файлу, не ключ | поиск по каталогу exe и, на Linux, `/etc` |
| `HOTEL_LISTEN` | `listen` | остаётся значение из ini или `127.0.0.1:8080` |
| `HOTEL_DSN` | `dsn` | не стирает `dsn` из ini |
| `HOTEL_WS_LISTEN` | `ws_listen` | не включает и не выключает сокет поверх ini |

`HOTEL_DSN=` в окружении Qt Creator или в systemd `EnvironmentFile` больше не обнуляет пароль, записанный в ini. Чтобы не ходить в базу, очистите ключ `dsn` или укажите другой файл в `HOTEL_CONFIG`.

Непустое значение побеждает файл целиком по этому ключу. В `config/hotel-api.env.example` строка `HOTEL_LISTEN=127.0.0.1:8080` не пустая, поэтому при подключённом env-файле адрес берётся из неё, а не из ini.

`HOTEL_CONFIG` не сливается с файлом рядом с exe. Указан другой ini — читается только он.

## Строки лога при старте

Без файла:

```text
hotel-api config none
hotel-api http 127.0.0.1 8080
hotel-api database not configured
```

Файл с DSN, сокет выключен:

```text
hotel-api config C:\hotel-api\hotel-api.ini
hotel-api http 127.0.0.1 8080
hotel-api database configured
```

На Linux путь будет вида `/etc/hotel-api/hotel-api.ini`. Слова `configured` / `not configured` говорят только о том, разобран ли DSN. Они не значат, что MariaDB ответила. Ответ базы виден в `GET /health`.

## Типичные сбои

### Кривой DSN или `listen` — процесс не слушает порт

В логе сначала путь файла (если файл открылся), затем одна строка ошибки из таблиц выше. Код выхода 1. HTTP нет, чинить нечего через `/health`. В тексте ошибки нет пароля и нет самой строки DSN.

`HOTEL_CONFIG` указывает в пустоту:

```text
HOTEL_CONFIG does not exist: C:\hotel-api\missing.ini
```

Файл есть, но учётка процесса не может его прочитать:

```text
could not read config file: C:\hotel-api\hotel-api.ini
```

Строки `hotel-api config none` в этих двух случаях нет.

### `driver_not_loaded` — нет плагина `QMYSQL`

`/health` отвечает `503`. Тело:

```json
{"status":"degraded","service":"hotel-api","version":"0.3.0","db":{"configured":true,"state":"down","error":"driver_not_loaded"}}
```

В логе: `database probe failed: driver_not_loaded`. Плагин Qt `QMYSQL` не загрузился. Так бывает, когда нет `qsqlmysql.dll` / `qsqlmysqld.dll` или Windows не смог подгрузить его зависимость `libmariadb.dll`.

Каталоги, откуда процесс их берёт:

| Файл | Куда положить |
|------|----------------|
| `qsqlmysql.dll` | `<каталог exe>\sqldrivers\qsqlmysql.dll` — сборка Release и служба |
| `qsqlmysqld.dll` | `<каталог exe>\sqldrivers\qsqlmysqld.dll` — Debug в Qt Creator (у отладочного Qt суффикс `d`) |
| `libmariadb.dll` | рядом с `hotel-api.exe`, не внутрь `sqldrivers` |

Из Qt Creator плагин часто уже виден в каталоге комплекта, например `C:\Qt\6.10.2\msvc2022_64\plugins\sqldrivers\`. Если в установщике Qt драйвер MySQL не отмечен, этого файла нет. `libmariadb.dll` комплект сам по пути exe не кладёт: её берут из MariaDB Connector/C (сборка, с которой собран плагин Qt) и копируют рядом с `hotel-api.exe`. Пока DLL лежит только в `sqldrivers`, загрузчик Windows её для зависимости плагина не находит, и симптом тот же: `driver_not_loaded`.

На Linux пакет `libqt6sql6-mysql` ставит `libqsqlmysql.so` в каталог плагинов Qt. Отдельный `libmariadb.dll` там не нужен; клиентская библиотека приходит из пакета. Без пакета `/health` даёт тот же `driver_not_loaded`.

### База не ответила — `503`, `connection_failed`

Плагин есть, DSN разобран, соединение не открылось. Таймаут 3 секунды.

`/health`:

```json
{"status":"degraded","service":"hotel-api","version":"0.3.0","db":{"configured":true,"state":"down","error":"connection_failed"}}
```

`POST /api/v1/sessions` в этом состоянии: `503`, `error` `database_unavailable`.

Сюда попадает недоступный хост, неверный порт, отказ в пароле, несуществующая база, файрвол. Тело ответа и лог не печатают DSN, поэтому по тексту не отличить «неверный пароль» от «порт закрыт». Проверка — `mariadb` с той же учётной записью с этой машины.

Пока в базе нет таблиц `nx_user` / `nx_session`, health может быть `up`, а логин отвечает `503` `session_store_unavailable`. Это уже схема, не ini. Миграция: `next/dbdump/migrations/0002_nx_core.sql`.

## Что не коммитить и кто файл читает

`hotel-api.ini` в дереве `next/` игнорируется git. Шаблон без пароля — `next/server/config/hotel-api.ini.example`. Заполненный ini, заполненный `hotel-api.env` и дамп с хешами в репозиторий не класть.

Linux, файл в `/etc/hotel-api/hotel-api.ini`, служба от пользователя `hotel-api`:

```bash
sudo chown root:hotel-api /etc/hotel-api/hotel-api.ini
sudo chmod 640 /etc/hotel-api/hotel-api.ini
```

Владелец читает и пишет, группа `hotel-api` читает, остальные не видят пароль. Каталог `/etc/hotel-api` должен пускать эту группу на чтение и вход (`chmod 750`, группа `hotel-api`).

Windows-служба по умолчанию — LocalSystem. Этой учётке нужны чтение exe, ini, `libmariadb.dll` и `sqldrivers\qsqlmysql.dll`. Если вход в службу сменён на доменную или локальную учётку, те же права чтения выдаются ей, и MariaDB должна принимать соединение с этой машины под пользователем из `dsn`. Пароль в ini лежит открытым текстом: каталог сборки Qt Creator для службы не используется, а сам файл не синхронизируется в git.
