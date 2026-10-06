# HANDOFF: миграция resort.next

Срез: **2026-10-06**, `main` после merge [PR #7](https://github.com/End1-1/resort.next/pull/7). Репозиторий: `End1-1/resort.next`. Документ самодостаточен вместе с деревом: секретов, паролей и боевых DSN здесь нет.

Новый агент продолжает работу только из этого файла и кода. Перед правками ещё раз посмотреть открытые PR: снимок «в работе» устаревает.

## 1. Цель

Сейчас в бою толстый клиент Qt/C++ (`Resort/`). Каждая станция после логина сама пишет SQL в MariaDB через `DoubleDatabase` (`Base/doubledatabase.h`). Отдельного прикладного сервера нет. `Server/` — tray UDP, не API. Разбор: [docs/audit/01-tekushchee-sostoyanie.md](../../docs/audit/01-tekushchee-sostoyanie.md).

Из-за этого уже есть:

- пароль MariaDB и дамп гостей попали в историю git;
- пароли пользователей — MD5 без соли (`users.f_password`);
- в старой схеме нет внешних ключей;
- SQL собирается склейкой строк (инъекции), см. [docs/audit/06-bystrye-pobedy.md](../../docs/audit/06-bystrye-pobedy.md) п. 6–7.

Цель: один процесс `hotel-api` (Qt/C++, каталог `next/server`) — **единственный** писатель в MariaDB. Клиенты ходят в HTTP REST/JSON по контракту [`next/server/openapi.yaml`](../server/openapi.yaml) (`/api/v1`) и в WebSocket только как подсказку «перечитай по HTTP». Десктоп переводится модуль за модулем (strangler fig). Старый клиент остаётся рабочим, пока флаг модуля не включён. Веб — тот же контракт, без второй копии правил.

## 2. Решения владельца

Они обязательны. Где они расходятся с аудитом, побеждает этот список.

| Решение | Где зафиксировано |
|---------|-------------------|
| Backend — Qt/C++ и CMake. Рекомендация Go из [docs/audit/03-celevaya-arhitektura.md](../../docs/audit/03-celevaya-arhitektura.md) §3.2 этим решением заменена. Windows: служба `HotelApi`. Linux: демон systemd (`Type=simple`, процесс на переднем плане, без double-fork). | [adr-0001-qt-cpp-server.md](adr-0001-qt-cpp-server.md) |
| Один процесс обслуживает REST и WebSocket. Бизнес-логики в Apache/PHP нет. PHP годится разве что как статика или обратный прокси. `smarthotel/` — не PMS. | этот файл; аудит §3.2 PHP как движок отвергает |
| Цель сборки — Qt 6.10. Владелец собирает Qt 6.10.2, MSVC 2022, Qt Creator, Windows, каталог сборки вида `D:\build.6.10.2\resort.next`. Те же исходники обязаны собираться на Qt 6.4+ под Linux (проверено на 6.4.2). | [next/server/README.md](../server/README.md), `httpserver.cpp` |
| Весь новый код миграции только в `next/`: `server`, `desktopapp`, `webport`, `docs`, `dbdump`. `Resort/`, `Server/`, `smarthotel/`, `DB/` не переписывать попутно. | [next/README.md](../README.md) |
| Схема с нуля, все таблицы с префиксом `nx_`. DDL: [`next/dbdump/migrations/0002_nx_core.sql`](../dbdump/migrations/0002_nx_core.sql). Карта имён: [nx-schema.md](nx-schema.md). Старые таблицы — только образец поведения. Expand/contract из аудита §2.4, который оставлял `f_reservation` / `m_register` хранилищем для `next/`, на этот API не действует. | [nx-schema.md](nx-schema.md) |
| `next/webport` начинается после того, как `next/desktopapp` реально работает. Каталога `webport/` в дереве нет. | ADR |
| Конфиг сервера: `hotel-api.ini` рядом с exe, на Linux ещё `/etc/hotel-api/hotel-api.ini`, либо полный путь в `HOTEL_CONFIG`. `HOTEL_LISTEN`, `HOTEL_DSN`, `HOTEL_WS_LISTEN` перекрывают ключ ini только если после обрезки пробелов не пустые. | [server-config.md](server-config.md) |
| Конфиг десктоп-клиента: INI в каталоге, куда пишет обычный пользователь (`%APPDATA%` на Windows, `~/.config` на Linux). Рядом с exe и в реестре HKLM его не класть. Рядом с exe допустим только необязательный ini значений по умолчанию, только на чтение. В клиентском ini — адрес API и локальные настройки. DSN и пароль MariaDB клиенту не выдаются. Пароль пользователя в файл не писать. | этот файл; в коде ещё нет, см. §5 |
| Язык UI — русский. Идентификаторы, ключи конфига и пути в коде — английские. | этот файл |

Ограничение к выбору Qt-сервера: `next/server` не линкует Qt Widgets и не компилирует диалоги `Resort/`. Правило переписывается кодом сервиса, а не копируется из формы.

Пароль в `nx_user` пока только схема `md5` (те же байты, что считает старый десктоп). Хеш сервис не переписывает на Argon2id: колонка старого клиента всё ещё MD5, смена хеша в SQL закроет смену. Аудит (фаза 2, «сразу заменить на Argon2id») здесь уступает этому ограничению, пока десктоп ходит в `users` сам.

## 3. Жёсткие правила для любого агента

- Не запускать `hotel-api`, миграции и скрипты сверки против **боевой** MariaDB без явного «да» владельца в этом разговоре. Копия базы — только если владелец её дал, а DSN не попадает в git.
- Историю git не переписывать и не делать force-push без **отдельного** явного «да». Секреты в истории всё ещё лежат. Чистка PR #4 касается только рабочего дерева.
- Пароли боевой MariaDB меняет сам владелец. Агент их не ротирует и не подставляет в коммиты.
- Не коммитить пароли, хеши, боевые DSN, дампы гостей, `hotel-api.ini`. Шаблон без секретов: `next/server/config/hotel-api.ini.example`. Политика дампов: [next/dbdump/README.md](../dbdump/README.md).
- Не менять логику кассы и фолио в `Resort/` (`winvoice`, конец дня, проводки, суммы). Точечные правки из бэклога §7 (bind, `exit(0)`, мусор сборки) — отдельные маленькие PR, удачный путь кассы не меняется.
- Один PR — одна тема. Ветка от `main`, PR в `main`.
- На Qt 6.10 `QSettings` с `IniFormat` считает комментарием только строку с `;`. Строка `#` без `=` даёт ошибку формата. На сервере это чинит [PR #8](https://github.com/End1-1/resort.next/pull/8); второй парсер ini не писать.

## 4. Что уже влито в `main`

Версия контракта: `0.3.0` (`openapi.yaml`, `HOTEL_API_VERSION`). Корневой `README.md` — changelog старого клиента `1.8.19.787`, не инструкция.

| PR | Статус | Что сделано |
|----|--------|-------------|
| [#1](https://github.com/End1-1/resort.next/pull/1) | merged | Аудит и план: `docs/audit/`. Код приложения не менялся. Движок в аудите — Go. |
| [#2](https://github.com/End1-1/resort.next/pull/2) | merged | Каркас `next/`: `hotel-api` (CMake, Qt 6), `GET /health`, заготовка сессий, OpenAPI, systemd, служба Windows, `hotel-desktop-stub`. |
| [#3](https://github.com/End1-1/resort.next/pull/3) | merged | Заголовки HTTP на Qt 6.8+/6.10 (`QHttpHeaders`, `bind`). Qt 6.4 оставлен на старом API. |
| [#4](https://github.com/End1-1/resort.next/pull/4) | merged | Логин по MD5 и чистка **рабочего дерева** (PHP, трекинг, тестер, дамп, скрины, `.idea`, UDP `who`). История не переписывалась. |
| [#5](https://github.com/End1-1/resort.next/pull/5) | merged | Схема `nx_` и перенос логина на неё. `0001` больше не используется. |
| [#6](https://github.com/End1-1/resort.next/pull/6) | merged | Чтение `hotel-api.ini` рядом с exe через `QSettings`. Пустая переменная окружения ключ не стирает. |
| [#7](https://github.com/End1-1/resort.next/pull/7) | merged | [server-config.md](server-config.md) по-русски. Кода нет. |

Поведение **сейчас** (после #5, не после #4):

- `GET /health` — без сессии. Нет DSN: `200`, `db.state=skipped`. DSN есть: проба `QMYSQL` (таймаут 3 с), `200` `up` или `503` `down`. Тело и лог без DSN и пароля.
- `GET /api/v1` — маркер `status=partial`.
- `POST /api/v1/sessions` — читает `nx_user`, пишет `nx_session`. Пароль в MariaDB не уходит. `password_scheme` должен быть `md5`, хеш — 32 hex MD5 от UTF-8. Таблицу `users` запрос не трогает, хеш не обновляет. Токен в ответе — 64 hex, в базе SHA-256, срок 12 часов UTC. `commands_allowed` ложен, если у роли нет строки `nx_role_permission`; токен всё равно выдаётся. Маршрута команд нет.
- Bearer на маршрутах **не проверяется**.
- WebSocket `/api/v1/ws` поднимается только при непустом `ws_listen`. Кадр hello, событий PMS нет.
- `nx_folio` и `nx_posting` есть в DDL. HTTP-маршрута фолио нет.
- `hotel-desktop-stub` — консоль без окон: `GET /health`, опционально один логин. Qt Sql не линкует, MariaDB не открывает. Это не UI ресепшена.
- `0001_hotel_api_session.sql` не применять. Сервис таблицу `hotel_api_session` не читает.
- В рабочем дереве `DB/db.sql` — DDL без `INSERT`. Ответ UDP `"who"` в исходнике `Server/dlgmain.cpp` пароль не содержит (порт `DATAGRAM_PORT` 33110 в `Base/defines.h`). Уже запущенный старый exe на объекте этим коммитом не обновляется — см. §6.

## 5. В работе прямо сейчас

Проверено по GitHub 2026-10-06: открыт один PR.

**(a) Парсер ini, открытый PR.** [PR #8](https://github.com/End1-1/resort.next/pull/8) (draft, ветка `cursor/ini-parser-dae8`). На Windows Qt 6.10.2 существующий `hotel-api.ini` (копия example с комментариями `#`) давал в логе `could not read config file`. На Qt 6.4 тот же файл читался. В PR `QSettings` в `next/server/src/config.cpp` заменяется своим чтением (`iniparse.cpp`): UTF-8, BOM, CRLF, комментарии `#` и `;`. В `main` всё ещё `QSettings`. Второй фикс поверх не начинать: доделать или ревьюить #8.

**(b) Первый оконный десктоп — в работе, открытого PR нет.** В `main` по-прежнему только `hotel-desktop-stub` (`QCoreApplication`, без Qt Widgets). Задуманное окно, которого в дереве ещё нет:

- диалог настроек подключения с проверкой `GET /health`;
- окно логина (`POST /api/v1/sessions`);
- главное окно.

Конфиг этого клиента — по правилу §2 (INI пользователя, не рядом с exe). Если к моменту чтения уже есть PR на `next/desktopapp` с окнами — продолжать его, второй клиент не открывать.

## 6. Открытые действия владельца

Агент это не делает сам.

1. Сменить пароли MariaDB на серверах (учётка приложения — не `root`). Раздать станциям новый профиль соединения. Старый пароль из истории git после ротации бесполезен только если он больше нигде не действует.
2. Прописать DSN в `hotel-api.ini` на машине (или `/etc/hotel-api/`), не в git.
3. Убедиться, что старый UDP-сервер на порту **33110** больше не отвечает паролем. Исходник в дереве уже молчит; живой процесс мог остаться старым бинарником.
4. Переписывание истории git (force-push, чтобы вычистить секреты из коммитов) — только после отдельного явного согласия. Пока согласия нет.

## 7. Бэклог по приоритету

Длинный план — [docs/audit/05-dorozhnaya-karta.md](../../docs/audit/05-dorozhnaya-karta.md) (фазы 0–10). Быстрые победы без нового backend — [docs/audit/06-bystrye-pobedy.md](../../docs/audit/06-bystrye-pobedy.md). Ниже порядок, который делать следующим. Каждый пункт — свой PR.

1. Скрипт только на чтение: сироты и дубли (бронь без гостя/номера, пересечение дат, проводка без брони, дубли `m_vaucher.f_code` и `users.f_username`, повтор прав). Рядом — сравнение старых строк и `nx_` после ручного копирования. Только `SELECT`, без `DELETE`. Файл в `next/dbdump/` (не в `migrations/`: их применяют как DDL). Результат с гостями в git не класть. Запуск на бою — только с «да» владельца; иначе копия.
2. `Base/baseuid.cpp`: при сбое генератора номера — сообщение в диалог и возврат ошибки, не `exit(0)`. Склейку SQL (`.arg`, `ap()`) перевести на bind `DoubleDatabase`. Удачный путь не менять.
3. Bind вместо склейки в `Resort/dlgclearlog.cpp` (имя пользователя) и `Resort/wreservationroomtab.cpp` (`delete` по id брони).
4. Убрать из сборки `Cache2` (`Resort/sources.cmake`) и несуществующий `../Selector` (`Resort/CMakeLists.txt`). `Server2` в эту пачку не брать.
5. Фаза 1: письменный протокол 15–20 реальных сценариев с цифрами (бронь, пересечение номера, заезд, RM, аванс, city ledger, отмена проводки, выезд с балансом, конец дня дважды, чек ККМ на тестовом аппарате, заказ ресторана на комнату, импорт Exely). Код смены не переписывать. Без протокола фолио (фаза 5) не начинать.
6. Read-эндпоинты API на таблицах `nx_`: шахматка и брони (фаза 3). Запись брони и фолио позже. Сверять с базой, не с кэшем станции.
7. Проверка bearer-токена на маршрутах. Сессия с `commands_allowed=false` на командах должна отвергаться. `GET /health` остаётся открытым.
8. Экраны десктопа поверх (b) из §5: шахматка и бронь по флагу, флаг по умолчанию выключен. `Resort/` до флага пишет SQL как раньше.
9. `next/webport` — после рабочего `desktopapp`. Касса и конец дня в браузере не открываются, пока они не прошли фазу 6 на десктопе.

Не делать в том же заходе: FK на живые `f_reservation` / `m_register`, переименование `m_vaucher`, правку MD5 в SQL так, чтобы старый десктоп перестал входить, веб-кассу, второй денежный писатель одного кода ваучера.

## 8. Как начать новому агенту

Прочитать по порядку:

1. Этот файл.
2. [adr-0001-qt-cpp-server.md](adr-0001-qt-cpp-server.md), [nx-schema.md](nx-schema.md), [server-config.md](server-config.md).
3. [docs/audit/05-dorozhnaya-karta.md](../../docs/audit/05-dorozhnaya-karta.md) и [docs/audit/06-bystrye-pobedy.md](../../docs/audit/06-bystrye-pobedy.md) — фон, не повод переписывать §2 и §7.
4. `gh pr list --state open` и дифф открытого PR, прежде чем трогать `config.cpp` или `desktopapp`.

Сборка и запуск сервера (Linux, Qt 6.4+, из корня репозитория):

```bash
sudo apt install cmake g++ ninja-build \
  qt6-base-dev qt6-httpserver-dev qt6-websockets-dev libqt6sql6-mysql
cmake -S next -B /tmp/hotel-next-build -G Ninja
cmake --build /tmp/hotel-next-build
/tmp/hotel-next-build/server/hotel-api
curl -sS http://127.0.0.1:8080/health
```

Без DSN health отвечает `200` и `db.state=skipped`. Это нормально. Боевой ini в репозиторий не класть: скопировать `hotel-api.ini.example` в `hotel-api.ini` рядом с бинарником и заполнить на машине. Подробности Windows (Qt Creator, служба, `qsqlmysql.dll`, `libmariadb.dll`) — в [server-config.md](server-config.md).

Заготовка клиента (не оконный UI):

```bash
cmake --build /tmp/hotel-next-build --target hotel-desktop-stub
HOTEL_API_BASE=http://127.0.0.1:8080 \
  /tmp/hotel-next-build/desktopapp/hotel-desktop-stub
```

Миграция схемы (не на бой без явного «да»; `0001` пропускать):

```bash
mariadb --default-character-set=utf8mb4 -h HOST -u USER -p DATABASE \
  < next/dbdump/migrations/0002_nx_core.sql
```

Пользователя миграция не вставляет. Шаблон с плейсхолдерами: `next/dbdump/seed/nx_user.example.sql`. Заполненную копию не коммитить.

На Windows владелец собирает из Qt Creator комплектом Qt 6.10.2 MSVC 2022, каталог сборки вида `D:\build.6.10.2\resort.next`. Исходники CMake — каталог `next`. Ini ищется рядом с `hotel-api.exe`, не в каталоге исходников.

Короткий промпт, который владелец вставляет новому помощнику:

```text
Прочитай next/docs/HANDOFF.md в End1-1/resort.next и продолжай миграцию
```
