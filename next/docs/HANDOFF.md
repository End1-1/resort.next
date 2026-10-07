# HANDOFF: миграция resort.next

Срез: **2026-10-06**, `main` после merge [PR #10](https://github.com/End1-1/resort.next/pull/10) (`2e18a0e`). Репозиторий: `End1-1/resort.next`. Документ самодостаточен вместе с деревом: секретов, паролей и боевых DSN здесь нет.

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
| Конфиг сервера: `hotel-api.ini` рядом с exe, на Linux ещё `/etc/hotel-api/hotel-api.ini`, либо полный путь в `HOTEL_CONFIG`. Ключи базы: `mysql_host`, `mysql_port` (необязателен, по умолчанию 3306), `mysql_schema`, `mysql_user`, `mysql_password`. Пароль буквальный, без percent-encoding. База включена, когда заданы хост и схема; без пользователя старт с ошибкой. `HOTEL_LISTEN`, `HOTEL_WS_LISTEN` и `HOTEL_MYSQL_HOST` / `PORT` / `SCHEMA` / `USER` / `PASSWORD` перекрывают ключ ini только если после обрезки пробелов не пустые. Старый `dsn` / `HOTEL_DSN` принимается только если нет ни одного непустого `mysql_host`/`port`/`schema`/`user`/`password`, и в лог пишется deprecation; если заданы оба, побеждают `mysql_*`. `mysql_ssl`: `off` \| `preferred` (по умолчанию) \| `required` \| `verify`, переменная `HOTEL_MYSQL_SSL`. По умолчанию локальная MariaDB без TLS должна открываться. Native `2026` — код `tls_error`. | [server-config.md](server-config.md) |
| Конфиг десктоп-клиента: личный INI, не рядом с exe и не в реестре (ни HKLM, ни HKCU). Windows: `%APPDATA%\Resort\hotel-desktop\hotel-desktop.ini`. Linux: `~/.config/Resort/hotel-desktop/hotel-desktop.ini` (или `$XDG_CONFIG_HOME/...`). Рядом с exe допустим только необязательный `hotel-desktop.ini` на чтение, и только пока личного файла нет. Ключи: `base_url`, `websocket_url`, `last_login`, `language` (`hy` / `en` / `ru`; пусто — язык системы, если он из этих трёх, иначе `ru`). DSN и пароль MariaDB клиенту не выдаются. Пароль и bearer в файл не писать. Комментарий в этом ini — только `;`. | `next/desktopapp/src/appconfig.cpp`, [next/desktopapp/README.md](../desktopapp/README.md) |
| Языки UI с первого дня: армянский (`hy`), английский (`en`), русский (`ru`). Это обязательно для всех клиентов: десктоп сейчас, `webport` позже. Исходные строки в коде английские. Сервер отдаёт языконезависимые коды ошибок (`database_not_configured` и другие); фразу показывает клиент. Имена справочников (типы номеров и т.п.) в одной колонке `name` для трёх языков не годятся — рекомендация, без смены DDL: [nx-schema.md](nx-schema.md). Идентификаторы, ключи конфига и пути — английские. | этот файл; [nx-schema.md](nx-schema.md) |

Ограничение к выбору Qt-сервера: `next/server` не линкует Qt Widgets и не компилирует диалоги `Resort/`. Правило переписывается кодом сервиса, а не копируется из формы.

Пароль в `nx_user` пока только схема `md5` (те же байты, что считает старый `Resort/`). Хеш сервис не переписывает на Argon2id: колонка старого клиента всё ещё MD5, смена хеша в SQL закроет смену. Аудит (фаза 2, «сразу заменить на Argon2id») здесь уступает этому ограничению, пока `Resort/` сам ходит в `users`.

## 3. Жёсткие правила для любого агента

- Не запускать `hotel-api`, миграции и скрипты сверки против **боевой** MariaDB без явного «да» владельца в этом разговоре. Копия базы — только если владелец её дал, а DSN не попадает в git.
- Историю git не переписывать и не делать force-push без **отдельного** явного «да». Секреты в истории всё ещё лежат. Чистка PR #4 касается только рабочего дерева.
- Пароли боевой MariaDB меняет сам владелец. Агент их не ротирует и не подставляет в коммиты.
- Не коммитить пароли, хеши, боевые DSN, дампы гостей, `hotel-api.ini`. Шаблон без секретов: `next/server/config/hotel-api.ini.example`. Политика дампов: [next/dbdump/README.md](../dbdump/README.md).
- Не менять логику кассы и фолио в `Resort/` (`winvoice`, конец дня, проводки, суммы). Точечные правки из бэклога §7 (bind, `exit(0)`, мусор сборки) — отдельные маленькие PR, удачный путь кассы не меняется.
- Один PR — одна тема. Ветка от `main`, PR в `main`.
- Комментарии в ini расходятся. Клиент читает файл через `QSettings::IniFormat`: на Qt 6.10 комментарий — только строка с `;`. Строка `#` без `=` — ошибка формата, файл отвергается, остаются встроенные значения. Серверный разбор (`next/server/src/iniparse.cpp`, [PR #8](https://github.com/End1-1/resort.next/pull/8)) принимает и `#`, и `;`. В `hotel-api.ini` решётка допустима. В `hotel-desktop.ini` и в его example — только `;`. Второй парсер для сервера не писать. Example клиента на `main` всё ещё с `#`; это чинит открытый [PR #11](https://github.com/End1-1/resort.next/pull/11), второй такой PR не открывать.

## 4. Что уже влито в `main`

Версия контракта: `0.4.0` (`openapi.yaml`, `HOTEL_API_VERSION`). Корневой `README.md` — changelog старого клиента `1.8.19.787`, не инструкция.

| PR | Статус | Что сделано |
|----|--------|-------------|
| [#1](https://github.com/End1-1/resort.next/pull/1) | merged | Аудит и план: `docs/audit/`. Код приложения не менялся. Движок в аудите — Go. |
| [#2](https://github.com/End1-1/resort.next/pull/2) | merged | Каркас `next/`: `hotel-api` (CMake, Qt 6), `GET /health`, заготовка сессий, OpenAPI, systemd, служба Windows, `hotel-desktop-stub`. |
| [#3](https://github.com/End1-1/resort.next/pull/3) | merged | Заголовки HTTP на Qt 6.8+/6.10 (`QHttpHeaders`, `bind`). Qt 6.4 оставлен на старом API. |
| [#4](https://github.com/End1-1/resort.next/pull/4) | merged | Логин по MD5 и чистка **рабочего дерева** (PHP, трекинг, тестер, дамп, скрины, `.idea`, UDP `who`). История не переписывалась. |
| [#5](https://github.com/End1-1/resort.next/pull/5) | merged | Схема `nx_` и перенос логина на неё. `0001` больше не используется. |
| [#6](https://github.com/End1-1/resort.next/pull/6) | merged | Первое чтение `hotel-api.ini` рядом с exe. Пустая переменная окружения ключ не стирает. Разбор через `QSettings` заменён в #8. |
| [#7](https://github.com/End1-1/resort.next/pull/7) | merged | [server-config.md](server-config.md) по-русски. Кода нет. |
| [#8](https://github.com/End1-1/resort.next/pull/8) | merged | Свой разбор `hotel-api.ini` (`iniparse.cpp`): UTF-8, BOM, CRLF, комментарии `#` и `;`. На Qt 6.10.2 `QSettings` отвергал живой файл с `#`. |
| [#9](https://github.com/End1-1/resort.next/pull/9) | merged | Первая версия этого handoff. |
| [#10](https://github.com/End1-1/resort.next/pull/10) | merged | Оконный `hotel-desktop`: настройки подключения, вход, главное окно. Личный ini. `hotel-desktop-stub` остался. |

Поведение **сейчас** (логин — как после #5, таблица `nx_user`, не `users`; ini сервера — #8; окна клиента — #10):

- `GET /health` — без сессии. Нет `mysql_host` и `mysql_schema`: `200`, `db.state=skipped`. Ключи заданы: проба `QMYSQL` (таймаут 3 с), `200` `up` или `503` `down`. Тело без пароля. Лог старта называет хост, порт, базу и пользователя, пароль не пишет.
- `GET /api/v1` — маркер `status=partial`.
- `POST /api/v1/sessions` — читает `nx_user`, пишет `nx_session`. Пароль в MariaDB не уходит. `password_scheme` должен быть `md5`, хеш — 32 hex MD5 от UTF-8. Таблицу `users` запрос не трогает, хеш не обновляет. Токен в ответе — 64 hex, в базе SHA-256, срок 12 часов UTC. `commands_allowed` ложен, если у роли нет строки `nx_role_permission`; токен всё равно выдаётся. Маршрута команд нет.
- Bearer проверяется на каждом `/api/v1/*`, кроме `POST /api/v1/sessions`. `GET /health` открыт. Токен — 64 hex, в базе SHA-256 (`nx_session.token_hash`). Нет токена, битый, чужой или отозванный: `401` `unauthorized`. Срок вышел: `401` `session_expired`. `nx_user.state` не `active`: `401` `user_disabled`. Изменяющий маршрут при `commands_allowed=false` — `403` `commands_not_allowed` (самого такого маршрута, кроме выхода, ещё нет; выход флаг не требует). `DELETE /api/v1/sessions` ставит `revoked_at`. `GET /api/v1/sessions/current` отдаёт пользователя без токена.
- Меню «Выход» вызывает `DELETE` и затем стирает токен из памяти. `401` на уже открытом сеансе возвращает на вход с переведённой фразой (`hy` / `en` / `ru`), не с текстом про неверный пароль.
- WebSocket `/api/v1/ws` поднимается только при непустом `ws_listen`. Кадр hello, событий PMS нет.
- `nx_folio` и `nx_posting` есть в DDL. HTTP-маршрута фолио нет.
- `hotel-desktop` — окна Qt Widgets, без Qt Sql и без MariaDB. «Настройки подключения» (адрес и необязательный WebSocket, «Проверить соединение» → `GET /health`), «Вход» (`POST /api/v1/sessions`), главное окно (пользователь, `role_id`, `commands_allowed`, адрес, опрос `/health` раз в 15 с, hello по WebSocket). Рабочая область пустая (`MainWindow::setWorkspacePage`). UI: `hy` / `en` / `ru`, переключение без перезапуска. Токен только в памяти. Коды ошибок сервера клиент переводит сам.
- Конфиг клиента — пути из §2. Ключ `password` при сохранении удаляется. Образец `hotel-desktop.ini.example` программа не открывает; на `main` его комментарии всё ещё `#` (см. §5).
- `hotel-desktop-stub` — консоль без окон: `GET /health`, опционально один логин. Ini клиента не читает. Это не UI ресепшена.
- `0001_hotel_api_session.sql` не применять. Сервис таблицу `hotel_api_session` не читает.
- В рабочем дереве `DB/db.sql` — DDL без `INSERT`. Ответ UDP `"who"` в исходнике `Server/dlgmain.cpp` пароль не содержит (порт `DATAGRAM_PORT` 33110 в `Base/defines.h`). Уже запущенный старый exe на объекте этим коммитом не обновляется — см. §6.

## 5. В работе прямо сейчас

Проверено по GitHub 2026-10-06: открыт [PR #11](https://github.com/End1-1/resort.next/pull/11) (не draft). Переводы `hy` / `en` / `ru` — отдельный PR поверх того же example.

[PR #11](https://github.com/End1-1/resort.next/pull/11) — «Use semicolon comments in hotel-desktop.ini.example», ветка `cursor/desktop-ini-semicolon-9e6f`, **OPEN**. `hotel-desktop` читает INI через `QSettings`. На Qt 6.10 строка `#` без `=` — ошибка формата, скопированный example отвергается, клиент берёт встроенные значения. В PR комментарии example и заметка в README переведены на `;`. Затронуты `next/desktopapp/hotel-desktop.ini.example`, `next/desktopapp/README.md`, `next/desktopapp/tests/login_smoke.cpp`. Серверный `hotel-api.ini.example` остаётся с `#`: его читает `iniparse`, не `QSettings`. Второй PR только на эту замену не открывать: доделать или влить #11.

Языки `hy` / `en` / `ru` для `hotel-desktop` — отдельная тема (решение в §2). Ветка с переводами включает коммит #11, чтобы example остался с `;` и получил ключ `language`. Если #11 уже влит, в новом PR остаётся только i18n.

Оконный клиент и парсер сервера уже в `main` (#10 и #8). Новое окно логина не начинать.

## 6. Открытые действия владельца

Агент это не делает сам.

1. Сменить пароли MariaDB на серверах (учётка приложения — не `root`). Раздать станциям новый профиль соединения. Старый пароль из истории git после ротации бесполезен только если он больше нигде не действует.
2. Прописать `mysql_host`, `mysql_schema`, `mysql_user` и `mysql_password` в `hotel-api.ini` на машине (или `/etc/hotel-api/`), не в git. Пароль писать как есть, без `%40`.
3. Убедиться, что старый UDP-сервер на порту **33110** больше не отвечает паролем. Исходник в дереве уже молчит; живой процесс мог остаться старым бинарником.
4. Переписывание истории git (force-push, чтобы вычистить секреты из коммитов) — только после отдельного явного согласия. Пока согласия нет.

## 7. Бэклог по приоритету

Длинный план — [docs/audit/05-dorozhnaya-karta.md](../../docs/audit/05-dorozhnaya-karta.md) (фазы 0–10). Быстрые победы без нового backend — [docs/audit/06-bystrye-pobedy.md](../../docs/audit/06-bystrye-pobedy.md). Сначала три пункта ниже, каждый своим PR. Они важнее остального списка.

1. Middleware bearer и эндпоинт выхода. **Сделано в 0.4.0.** Токен из `POST /api/v1/sessions` проверяется на `/api/v1/*`; `GET /health` открыт. `commands_allowed=false` на изменяющих маршрутах даёт `403` `commands_not_allowed` (чтение и выход — нет). `DELETE /api/v1/sessions` пишет `revoked_at`. Клиент вызывает выход, а не только чистит память.
2. Read-эндпоинты на таблицах `nx_`: шахматка и брони (фаза 3). Запись брони и фолио позже. Сверять с базой, не с кэшем станции.
3. Первый экран десктопа в пустой области `MainWindow::setWorkspacePage` (шахматка или список броней), по флагу, флаг по умолчанию выключен. `Resort/` до флага пишет SQL как раньше. Экран не открывает MariaDB.

Дальше, не перебивая пункты 1–3:

4. Скрипт только на чтение: сироты и дубли (бронь без гостя/номера, пересечение дат, проводка без брони, дубли `m_vaucher.f_code` и `users.f_username`, повтор прав). Рядом — сравнение старых строк и `nx_` после ручного копирования. Только `SELECT`, без `DELETE`. Файл в `next/dbdump/` (не в `migrations/`: их применяют как DDL). Результат с гостями в git не класть. Запуск на бою — только с «да» владельца; иначе копия.
5. `Base/baseuid.cpp`: при сбое генератора номера — сообщение в диалог и возврат ошибки, не `exit(0)`. Склейку SQL (`.arg`, `ap()`) перевести на bind `DoubleDatabase`. Удачный путь не менять.
6. Bind вместо склейки в `Resort/dlgclearlog.cpp` (имя пользователя) и `Resort/wreservationroomtab.cpp` (`delete` по id брони).
7. Убрать из сборки `Cache2` (`Resort/sources.cmake`) и несуществующий `../Selector` (`Resort/CMakeLists.txt`). `Server2` в эту пачку не брать.
8. Фаза 1: письменный протокол 15–20 реальных сценариев с цифрами (бронь, пересечение номера, заезд, RM, аванс, city ledger, отмена проводки, выезд с балансом, конец дня дважды, чек ККМ на тестовом аппарате, заказ ресторана на комнату, импорт Exely). Код смены не переписывать. Без протокола фолио (фаза 5) не начинать.
9. `next/webport` — после экранов `desktopapp`. Касса и конец дня в браузере не открываются, пока они не прошли фазу 6 на десктопе.

Не делать в том же заходе: FK на живые `f_reservation` / `m_register`, переименование `m_vaucher`, правку MD5 в SQL так, чтобы старый десктоп перестал входить, веб-кассу, второй денежный писатель одного кода ваучера.

## 8. Как начать новому агенту

Прочитать по порядку:

1. Этот файл.
2. [adr-0001-qt-cpp-server.md](adr-0001-qt-cpp-server.md), [nx-schema.md](nx-schema.md), [server-config.md](server-config.md).
3. [docs/audit/05-dorozhnaya-karta.md](../../docs/audit/05-dorozhnaya-karta.md) и [docs/audit/06-bystrye-pobedy.md](../../docs/audit/06-bystrye-pobedy.md) — фон, не повод переписывать §2 и §7.
4. `gh pr list --state open`. Сейчас это [PR #11](https://github.com/End1-1/resort.next/pull/11): не трогать `hotel-desktop.ini.example` вторым коммитом на ту же тему. `config.cpp` сервера уже на `iniparse`.

Сборка и запуск сервера (Linux, Qt 6.4+, из корня репозитория):

```bash
sudo apt install cmake g++ ninja-build \
  qt6-base-dev qt6-httpserver-dev qt6-websockets-dev libqt6sql6-mysql
cmake -S next -B /tmp/hotel-next-build -G Ninja
cmake --build /tmp/hotel-next-build
/tmp/hotel-next-build/server/hotel-api
curl -sS http://127.0.0.1:8080/health
```

Без `mysql_host` и `mysql_schema` health отвечает `200` и `db.state=skipped`. Это нормально. Боевой ini в репозиторий не класть: скопировать `hotel-api.ini.example` в `hotel-api.ini` рядом с бинарником и заполнить на машине. Подробности Windows (Qt Creator, служба, `qsqlmysql.dll`, `libmariadb.dll`) — в [server-config.md](server-config.md).

Оконный клиент и консольная заготовка (сначала поднять `hotel-api`):

```bash
cmake --build /tmp/hotel-next-build --target hotel-desktop
cmake --build /tmp/hotel-next-build --target hotel-desktop-stub
/tmp/hotel-next-build/desktopapp/hotel-desktop
HOTEL_API_BASE=http://127.0.0.1:8080 \
  /tmp/hotel-next-build/desktopapp/hotel-desktop-stub
```

`hotel-desktop` открывает окно входа. Адрес по умолчанию `http://127.0.0.1:8080`, пока нет личного ini. На Linux без дисплея: `QT_QPA_PLATFORM=offscreen`. Настройки пишутся в `%APPDATA%\Resort\hotel-desktop\hotel-desktop.ini` или `~/.config/Resort/hotel-desktop/hotel-desktop.ini`, не рядом с exe. В этом файле комментарии только через `;`. Комментарии `#` — для `hotel-api.ini`, его читает сервер. Stub ini клиента не читает. Подробности Windows (`windeployqt`, WebSockets в комплекте) — в [next/desktopapp/README.md](../desktopapp/README.md).

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
