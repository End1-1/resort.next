# Файл `hotel-api.ini`

`hotel-api` читает настройки из `hotel-api.ini`. Переменные окружения для старта не обязательны. Имя файла именно `hotel-api.ini`: рядом лежащий `hotel-api.ini.example` процесс не открывает.

Ключи пишутся латиницей в корне файла или в секции `[General]` / `[hotel-api]`. Другая секция — ошибка разбора, ключи из неё не читаются молча.

Пустой файл тоже считается найденным. Тогда действуют встроенные значения: слушать `127.0.0.1:8080`, базу не открывать, WebSocket выключен.

## Кодировка и комментарии

Файл сохраняйте как **UTF-8**. Годятся перевод строки LF и CRLF (Блокнот Windows) и метка UTF-8 BOM в начале файла.

На Qt 6.10 процесс раньше отдавал файл в `QSettings`. У этого разбора комментарий — только строка, которая начинается с `;`. Строка с `#` и без знака `=` считается сломанной (`FormatError`). Шаблон `hotel-api.ini.example` и примеры в этой заметке как раз состоят из таких строк с `#`, поэтому при живом файле на диске лог был `could not read config file: ...\hotel-api.ini`. Свой разбор этот класс больше не использует.

Комментарий — целая строка: первый непробельный символ `#` или `;`. Знак `#` или `;` внутри значения не отрезается, иначе пароль с этими символами обрежется.

Значение берётся буквально. `%40` остаётся четырьмя символами `%`, `4`, `0`: пароль больше не кодируется как URL. Если всё значение в двойных кавычках, одна пара кавычек снимается, пробелы внутри сохраняются: `" p@ss "` — это пароль из пробела, `p@ss` и пробела.

Блокнот в режиме «Юникод» пишет UTF-16 (BOM `FF FE` или `FE FF`). Такой файл не читается, в логе:

```text
file is UTF-16; save as UTF-8: D:\build.6.10.2\resort.next\debug\server\hotel-api.ini
```

Сохраните как UTF-8. Файл, который не является UTF-8, даёт `file is not UTF-8; save as UTF-8`.

## Куда класть файл

Поиск идёт сверху вниз. Берётся первый существующий файл. Файл, который не открывается или не разбирается, останавливает процесс и не пропускает поиск к следующему кандидату.

1. `HOTEL_CONFIG`, если переменная задана и после обрезки пробелов не пустая. Это полный путь к файлу, он заменяет шаги 2 и 3. Файла нет — процесс завершается, в логе `HOTEL_CONFIG does not exist: <путь>`.
2. `hotel-api.ini` в каталоге исполняемого файла. Это каталог `hotel-api.exe` / `hotel-api`, не текущий рабочий каталог процесса.
3. Только Linux: `/etc/hotel-api/hotel-api.ini`.
4. Ни одного файла нет. Лог: `hotel-api config none`. Слушать `127.0.0.1:8080`, база не настроена, WebSocket выключен.

В логе при успехе одна строка `hotel-api config` и абсолютный путь. В этой строке нет пароля.

### Qt Creator, Debug, Windows

Рабочий каталог запуска в Kits может быть каталогом исходников. На настройки это не влияет: читается файл рядом с `hotel-api.exe`.

Каталог exe — выход сборки, не `next/server/config/`. У MSVC это обычно папка `Debug` или `Release` внутри build. CMake копирует туда `hotel-api.ini.example` (не боевой ini). Скопируйте его в `hotel-api.ini` в ту же папку и впишите `mysql_host`, `mysql_schema`, `mysql_user` и `mysql_password`.

Пока рядом с exe нет `hotel-api.ini` и `HOTEL_CONFIG` пуст, лог будет `hotel-api config none` и `hotel-api database not configured`. `GET /health` при этом отвечает `200` и `"db":{"configured":false,"state":"skipped"}`. Это не обрыв связи с MariaDB.

### Служба Windows

Служба `HotelApi` стартует с рабочим каталогом `System32`. Ini из `System32` не читается. Файл кладётся рядом с `hotel-api.exe`, тем же правилом, что и в отладчике.

Установка (`hotel-api --install`) регистрирует службу от **LocalSystem** (учётная запись в `CreateService` не задаётся). LocalSystem читает каталоги вроде `C:\hotel-api\`. Каталог сборки в профиле пользователя (`C:\Users\...\`) служба часто прочитать не может. Для службы держите exe, ini и `libmariadb.dll` в отдельном каталоге, доступном LocalSystem, либо той учётной записи, под которой службу перевели.

### Linux

Рядом с бинарником, например `/usr/local/bin/hotel-api.ini`, файл находится раньше, чем `/etc/hotel-api/hotel-api.ini`. Для пароля удобнее один файл в `/etc/hotel-api/` и отсутствие копии рядом с бинарником: иначе правка в `/etc` не подхватится.

Юнит `next/server/deploy/hotel-api.service` запускает `/usr/local/bin/hotel-api` от пользователя `hotel-api`. `EnvironmentFile=/etc/hotel-api/hotel-api.env` по-прежнему только для переменных; сам ini этот файл не заменяет.

## Пример

Боевой файл. Пароль сюда подставляется на сервере, в git он не коммитится. Пробелы вокруг `=` можно ставить, как у `mysql_user`.

```ini
listen=127.0.0.1:8080

mysql_host=127.0.0.1
mysql_schema=hotelnext
mysql_user = root
mysql_password=secret

ws_listen=8081
```

`ws_listen=8081` — то же, что `127.0.0.1:8081`. `mysql_port` в примере нет, значит порт MariaDB `3306`. Пароль `secret` записан как есть: знаки `@`, `:`, `%`, `#`, `;` в пароле кодировать не нужно.

Тот же смысл с выключенной базой (health не ходит в MariaDB): не пишите `mysql_host` и `mysql_schema`, либо оставьте их пустыми.

```ini
listen=127.0.0.1:8080
mysql_host=
mysql_schema=
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

## Ключи MariaDB

База настроена, когда после обрезки пробелов непусты и `mysql_host`, и `mysql_schema`. Тогда обязателен и `mysql_user`: без него процесс не слушает порт. `mysql_password` может быть пустым. `mysql_port` необязателен, пустое значение и отсутствие ключа значат `3306`.

| Ключ | Переменная | Смысл |
|------|------------|--------|
| `mysql_host` | `HOTEL_MYSQL_HOST` | Хост MariaDB, как его видит процесс. Для TCP на этой машине обычно `127.0.0.1`. |
| `mysql_port` | `HOTEL_MYSQL_PORT` | Порт от 1 до 65535. Нет ключа — `3306`. |
| `mysql_schema` | `HOTEL_MYSQL_SCHEMA` | Имя базы, например `hotelnext`. |
| `mysql_user` | `HOTEL_MYSQL_USER` | Пользователь MariaDB. |
| `mysql_password` | `HOTEL_MYSQL_PASSWORD` | Пароль буквально. В лог не пишется. |
| `mysql_ssl` | `HOTEL_MYSQL_SSL` | `off`, `preferred`, `required` или `verify`. Нет ключа — `preferred`. |
| `mysql_ssl_ca` | `HOTEL_MYSQL_SSL_CA` | Файл CA. Передаётся драйверу только при `required` и `verify`. В значении нельзя `;`. |

Значение не проходит через URL. Пароль `p@ss:w%rd#;x` в файле выглядит так же. `#` и `;` внутри значения не начинают комментарий: комментарий — только целая строка. Пробелы по краям пароля сохраняются, если значение в кавычках: `mysql_password=" p@ss "`.

`mysql_ssl` без ключа или пустой — это `preferred`. Так локальная MariaDB без TLS открывается сама: клиент берёт шифрование, если сервер его предлагает, и обычное соединение, если нет. Сертификат при этом не проверяется. `off` — не требовать TLS. `required` — TLS обязателен. `verify` — TLS обязателен и сертификат сервера сверяется с `mysql_ssl_ca` (если файл не задан — с системным хранилищем). Чужое слово в `mysql_ssl` обрывает старт: `mysql_ssl must be off, preferred, required, or verify`.

Какие опции Qt 6.10.2 реально передаёт в `mysql_options`, зависит от того, с какой клиентской библиотекой собран плагин `QMYSQL` (`src/plugins/sqldrivers/mysql/qsql_mysql.cpp`):

| Режим | `MYSQL_OPT_SSL_MODE` (только libmysqlclient 5.7.11+ / 8.x) | `MYSQL_OPT_SSL_VERIFY_SERVER_CERT` (MariaDB Connector/C; у MySQL 8 этого пункта в драйвере нет) |
|-------|--------------------------------------------------------------|--------------------------------------------------------------------------------------------------|
| `off` | `DISABLED` | `0` |
| `preferred` | `PREFERRED` | `0` |
| `required` | `REQUIRED` | `1`, и `MYSQL_OPT_SSL_CA`, если задан файл |
| `verify` | `VERIFY_CA` | `1`, и `MYSQL_OPT_SSL_CA`, если задан файл |

Плагин, собранный с MariaDB Connector/C, строку `MYSQL_OPT_SSL_MODE` не принимает: в исходнике Qt она закрыта условием `!defined(MARIADB_VERSION_ID)`. Если её всё же передать, Qt пишет на каждое соединение `Illegal connect option value 'MYSQL_OPT_SSL_MODE=PREFERRED'`. hotel-api эту строку не передаёт, когда загруженный `qsqlmysql` связан с MariaDB: в таблице импорта есть `libmariadb`, либо `mysql_get_client_info()` содержит `MariaDB` или начинается с `3.` (Connector/C 3.x; у libmysqlclient это `8.0.x` или `5.7.x`). Для libmysqlclient `MYSQL_OPT_SSL_MODE` по-прежнему передаётся. Рабочий переключатель на Connector/C — `MYSQL_OPT_SSL_VERIFY_SERVER_CERT`. У Connector/C 3.4 он по умолчанию включён, и тогда сервер без TLS даёт native `2026`. Значение `0` это снимает. У Connector/C 3.3 флаг и так выключен. `off` и `preferred` на такой сборке поэтому одинаковы: TLS не обязателен, а если сервер его предлагает, клиент может его включить. Полностью запретить TLS (`DISABLED`) умеет только libmysqlclient.

Пустые `mysql_host` и `mysql_schema` (или ключей нет) — база не настроена. `GET /health` даёт `200`, `db.state=skipped`. `POST /api/v1/sessions` даёт `503` и `error` `database_not_configured`. В логе: `hotel-api database not configured`.

Если задан хост или схема, а второго нет, старт обрывается: `mysql_host is required` или `mysql_schema is required`. Нет пользователя при заданных хосте и схеме: `mysql_user is required`. Порт не число или вне 1…65535: `mysql_port has an invalid port`. В тексте ошибки нет пароля.

Удачный старт с базой:

```text
hotel-api database 127.0.0.1:3306/hotelnext user=root
```

В этой строке есть хост, порт, имя базы и пользователь. Пароля нет. Строка не значит, что MariaDB уже ответила: это только то, что прочитано из ini. Ответ базы виден в `GET /health`.

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
| `HOTEL_MYSQL_HOST` | `mysql_host` | не стирает хост из ini |
| `HOTEL_MYSQL_PORT` | `mysql_port` | не стирает порт из ini |
| `HOTEL_MYSQL_SCHEMA` | `mysql_schema` | не стирает имя базы из ini |
| `HOTEL_MYSQL_USER` | `mysql_user` | не стирает пользователя из ini |
| `HOTEL_MYSQL_PASSWORD` | `mysql_password` | не стирает пароль из ini |
| `HOTEL_MYSQL_SSL` | `mysql_ssl` | не стирает режим из ini |
| `HOTEL_MYSQL_SSL_CA` | `mysql_ssl_ca` | не стирает путь CA из ini |
| `HOTEL_WS_LISTEN` | `ws_listen` | не включает и не выключает сокет поверх ini |

`HOTEL_MYSQL_PASSWORD=` в окружении Qt Creator или в systemd `EnvironmentFile` не обнуляет пароль, записанный в ini. Чтобы не ходить в базу, очистите `mysql_host` и `mysql_schema` или укажите другой файл в `HOTEL_CONFIG`.

Непустое значение побеждает файл целиком по этому ключу. В `config/hotel-api.env.example` строка `HOTEL_LISTEN=127.0.0.1:8080` не пустая, поэтому при подключённом env-файле адрес берётся из неё, а не из ini.

`HOTEL_CONFIG` не сливается с файлом рядом с exe. Указан другой ini — читается только он.

## Строки лога при старте

Без файла:

```text
hotel-api config none
hotel-api http 127.0.0.1 8080
hotel-api database not configured
```

Файл с ключами MariaDB, сокет выключен:

```text
hotel-api config C:\hotel-api\hotel-api.ini
hotel-api http 127.0.0.1 8080
hotel-api database 127.0.0.1:3306/hotelnext user=root
```

На Linux путь будет вида `/etc/hotel-api/hotel-api.ini`. Строка `hotel-api database` называет хост, порт, базу и пользователя. Пароля в ней нет. Она не значит, что MariaDB ответила. Ответ базы виден в `GET /health`.

### Старый ключ `dsn`

Новый файл его не содержит. Если ключа `mysql_*` с непустым значением нет, непустой `dsn` или непустой `HOTEL_DSN` ещё принимается, и в лог пишется:

```text
hotel-api database: dsn is deprecated; use mysql_host, mysql_port, mysql_schema, mysql_user, mysql_password
```

Если заданы и ключи `mysql_*`, и `dsn` / `HOTEL_DSN`, побеждают `mysql_*`, а в логе:

```text
hotel-api database: mysql_* overrides dsn
```

Пароль в этих двух строках не печатается. В старом `dsn` знаки `@` и `:` в пароле по-прежнему надо было писать как `%40` и `%3A`. В `mysql_password` этого делать не нужно.

## Типичные сбои

### Кривые `listen` или ключи MariaDB — процесс не слушает порт

В логе сначала путь файла (если файл открылся), затем одна строка ошибки. Код выхода 1. HTTP нет, чинить нечего через `/health`. В тексте ошибки нет пароля.

`HOTEL_CONFIG` указывает в пустоту:

```text
HOTEL_CONFIG does not exist: C:\hotel-api\missing.ini
```

Файл есть, но учётка процесса не может его прочитать. В тексте есть причина от `QFile`:

```text
cannot open C:\hotel-api\hotel-api.ini: Access is denied.
```

Строка без `=`, которая не комментарий и не секция:

```text
line 12 malformed: C:\hotel-api\hotel-api.ini
```

Чужая секция или неизвестный ключ:

```text
line 4: unknown section [database]: C:\hotel-api\hotel-api.ini
line 5: unknown key "listn": C:\hotel-api\hotel-api.ini
```

Строки `hotel-api config none` в этих случаях нет: путь уже назван в строке ошибки.

### `driver_not_loaded` — нет плагина `QMYSQL`

`/health` отвечает `503`. Тело:

```json
{"status":"degraded","service":"hotel-api","version":"0.6.0","db":{"configured":true,"state":"down","error":"driver_not_loaded"}}
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

### База не ответила — `503`

Плагин есть, ключи MariaDB прочитаны, `db.open()` не прошёл. Таймаут подключения, чтения и записи — 3 секунды (`MYSQL_OPT_CONNECT_TIMEOUT`, `MYSQL_OPT_READ_TIMEOUT`, `MYSQL_OPT_WRITE_TIMEOUT`). Эти опции понимает MariaDB Connector/C 10.x и 11.x, в том числе на Windows. `MYSQL_SET_CHARSET_NAME` в опции не ставится: драйвер Qt её игнорирует, кодировка включается командой `SET NAMES utf8mb4` уже после успешного открытия.

Соединение создаётся на потоке HTTP-обработчика, с уникальным именем `mysql-<uuid>`, и снимается в деструкторе. Это не общая «default connection» и не чужой поток. Такое имя само по себе `connection_failed` не даёт.

В лог пишется номер ошибки MariaDB и текст драйвера. Пароль в эту строку не попадает: если он встретился в тексте, на его месте `***`. Хост, порт, имя базы и пользователь в логе есть. В JSON их нет — только короткий код.

Пример строки при отказе в доступе (1045):

```text
database connect failed: code=access_denied native=1045 host=127.0.0.1 port=3306 database=hotelnext user=hotel_api driver=Access denied for user 'hotel_api'@'127.0.0.1' (using password: YES) server=Access denied for user 'hotel_api'@'127.0.0.1' (using password: YES)
database probe failed: access_denied
```

`/health` в том же случае:

```json
{"status":"degraded","service":"hotel-api","version":"0.6.0","db":{"configured":true,"state":"down","error":"access_denied"}}
```

`POST /api/v1/sessions` отвечает `503` с тем же машинным кодом в поле `error` (`access_denied`, `unknown_database` или `cannot_connect`). Текст `message` общий, без хоста и без пароля. Номер, которому нет отдельного кода, по-прежнему даёт в `/health` `connection_failed`, а на входе `database_unavailable`.

| Номер MariaDB | Код в `/health` и во входе | Что проверить |
|---------------|----------------------------|---------------|
| 1045 | `access_denied` | Учётка и хост. `'hotel_api'@'127.0.0.1'` и `'hotel_api'@'localhost'` — разные пользователи. Права, выданные на `localhost` (сокет или именованный канал), TCP на `127.0.0.1` не покрывают. Другие программы при этом могут входить, а этот хост получает Access denied. Нужен пользователь именно для `mysql_host`, с правом на базу `mysql_schema`. |
| 1049 | `unknown_database` | Базы из `mysql_schema` нет. Создать её и применить `next/dbdump/migrations/0002_nx_core.sql`. |
| 2003 | `cannot_connect` | До порта никто не принял TCP: служба не запущена, слушает только сокет или именованный канал, другой порт, файрвол. |
| 2026 | `tls_error` | Клиент потребовал TLS, а сервер его не даёт (`SSL is required, but the server does not support it`). Для локальной MariaDB без TLS оставьте `mysql_ssl=preferred` (это значение по умолчанию) или поставьте `off`. Либо включите TLS на сервере и для проверки сертификата укажите `mysql_ssl=verify` и `mysql_ssl_ca`. |
| 2002 | `cannot_connect` | Клиент не открыл локальный сокет или канал. Для хоста `127.0.0.1` обычно приходит 2003, не 2002. |
| другой | `connection_failed` в `/health`, `database_unavailable` на входе | Смотреть `native=` и `driver=` в логе. |

Строка Windows `nlansp_c.dll` с кодом `8007277C` («No such service is known») — это Winsock NLA (Network Location Awareness), Win32 `WSASERVICE_NOT_FOUND` (10108). Так бывает, когда служба NLA не запущена и провайдер имён не загрузился. Это не номер MariaDB и не причина `connection_failed`.

Пока в базе нет таблиц `nx_user` / `nx_session`, health может быть `up`, а логин отвечает `503` `session_store_unavailable`. Это уже схема, не ini. Миграция: `next/dbdump/migrations/0002_nx_core.sql`.

## Что не коммитить и кто файл читает

`hotel-api.ini` в дереве `next/` игнорируется git. Шаблон без пароля — `next/server/config/hotel-api.ini.example`. Заполненный ini, заполненный `hotel-api.env` и дамп с хешами в репозиторий не класть.

Linux, файл в `/etc/hotel-api/hotel-api.ini`, служба от пользователя `hotel-api`:

```bash
sudo chown root:hotel-api /etc/hotel-api/hotel-api.ini
sudo chmod 640 /etc/hotel-api/hotel-api.ini
```

Владелец читает и пишет, группа `hotel-api` читает, остальные не видят пароль. Каталог `/etc/hotel-api` должен пускать эту группу на чтение и вход (`chmod 750`, группа `hotel-api`).

Windows-служба по умолчанию — LocalSystem. Этой учётке нужны чтение exe, ini, `libmariadb.dll` и `sqldrivers\qsqlmysql.dll`. Если вход в службу сменён на доменную или локальную учётку, те же права чтения выдаются ей, и MariaDB должна принимать соединение с этой машины под пользователем из `mysql_user`. Пароль в ini лежит открытым текстом: каталог сборки Qt Creator для службы не используется, а сам файл не синхронизируется в git.
