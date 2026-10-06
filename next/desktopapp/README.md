# next/desktopapp

Qt Widgets client for `hotel-api`, plus the headless `hotel-desktop-stub`. CMake only. The client speaks HTTP JSON (and an optional WebSocket hello). It does not link Qt Sql and it does not open MariaDB. Business rules stay in `next/server`.

`hotel-desktop` is the windowed program: connection settings, login, and an empty workspace where later screens (rack chart, reservations) will go. `hotel-desktop-stub` remains a console check of `GET /health` and, optionally, one login call. `Resort/` is unchanged and still writes SQL.

`next/webport` is still not in this tree. The window shell is usable; the reception screens are not.

## Layout

| Piece | Role |
|-------|------|
| `src/apiclient.*`, `src/urlutil.*` | Base URL, in-memory bearer token, async `GET /health` and `POST /api/v1/sessions`, Russian error mapping. No widgets. |
| `src/appconfig.*` | Per-user INI. Never stores a password or a token. |
| `src/connectiondialog.*` | Address, WebSocket, «Проверить соединение». |
| `src/loginwindow.*` | Login form. |
| `src/mainwindow.*` | Session summary, menus, `QStackedWidget` workspace. |
| `src/healthmonitor.*` | Periodic `/health` and optional WebSocket hello. |
| `src/appcontroller.*` | Shows login, then the main window. |
| `hotel-desktop.ini.example` | Commented sample. The program does not read this filename. |

A later screen is a `QWidget` that takes `ApiClient *` and is passed to `MainWindow::setWorkspacePage`. It must not open a database.

## Build

Qt 6.4 or newer: **Core, Gui, Widgets, Network, WebSockets**. HttpServer and Sql are not linked. C++17, CMake 3.21+.

The owner kit is Qt 6.10.2 MSVC 2022, opened from Qt Creator. The same sources build on Qt 6.4 Linux.

Linux packages (Ubuntu 24.04):

```bash
sudo apt install cmake g++ ninja-build qt6-base-dev qt6-websockets-dev
```

From the repository root:

```bash
cmake -S next -B /tmp/hotel-next-build -G Ninja
cmake --build /tmp/hotel-next-build --target hotel-desktop
cmake --build /tmp/hotel-next-build --target hotel-desktop-stub
```

Building `next/desktopapp` on its own also works (`cmake -S next/desktopapp`).

### Windows (Qt Creator)

1. Install the Qt 6.10.2 **MSVC 2022 64-bit** kit, including the **WebSockets** module.
2. Open `next/CMakeLists.txt` (not a `.pro`).
3. Select the `hotel-desktop` target and press Run.

The executable is `WIN32_EXECUTABLE`, so it starts as a GUI process (no extra console window) via `qt_add_executable(... WIN32)`, which supplies `WinMain`. Russian source strings are compiled with `/utf-8`.

To copy the program out of the build tree, run `windeployqt` from the **same** kit on the built exe:

```bat
windeployqt --release --compiler-runtime path\to\hotel-desktop.exe
```

That copies Qt Widgets, Network, WebSockets, the platform plugin, and the TLS plugin next to the exe. Settings are still stored under `%APPDATA%`, not next to the exe. `http://` does not need TLS. An `https://` base URL is accepted and stored for later; it needs the TLS plugin `windeployqt` copies.

`hotel-desktop-stub` is a console program. Do not pass it to `windeployqt` unless you want the Qt Network DLLs beside it.

## Run

Start `hotel-api` first (`../server/README.md`). Then run `hotel-desktop`. The login window opens on `http://127.0.0.1:8080` until a settings file says otherwise.

On Linux without a display:

```bash
QT_QPA_PLATFORM=offscreen /tmp/hotel-next-build/desktopapp/hotel-desktop
```

The stub:

```bash
HOTEL_API_BASE=http://127.0.0.1:8080 /tmp/hotel-next-build/desktopapp/hotel-desktop-stub
```

`HOTEL_API_BASE` is an origin with no path. Default `http://127.0.0.1:8080`. The stub prints the `/health` body and exits 0 on HTTP 200. It does not read the INI below.

Optional login (both variables, or neither). The token and the password are not printed:

```bash
HOTEL_API_BASE=http://127.0.0.1:8080 \
HOTEL_LOGIN=USER \
HOTEL_PASSWORD=SECRET \
  /tmp/hotel-next-build/desktopapp/hotel-desktop-stub
```

Do not commit `HOTEL_PASSWORD`.

## Файл настроек

Личный INI, `QSettings::IniFormat`, не реестр и не каталог программы. Каталог создаётся при первой записи. Пароль и токен туда не пишутся. Ключ `password`, если его добавить руками, при сохранении удаляется.

| ОС | Путь |
|----|------|
| Windows | `%APPDATA%\Resort\hotel-desktop\hotel-desktop.ini` |
| Linux | `~/.config/Resort/hotel-desktop/hotel-desktop.ini` |

На Linux при заданном `XDG_CONFIG_HOME` каталог начинается с него, а не с `~/.config`. Имя организации для `QSettings` — `Resort`, имя приложения — `hotel-desktop`. Формат файла всё равно INI по полному пути: на Windows это не `HKCU` и не `HKLM`.

Окно «Настройки подключения» показывает этот путь целиком. Кнопка «Проверить соединение» вызывает `GET /health` по адресу из полей (ещё не обязательно сохранённому) и пишет состояние сервера и базы: `up`, `down`, `skipped`, `driver_not_loaded`.

Ключи в секции `[General]` (без другой секции):

```ini
base_url=http://127.0.0.1:8080
websocket_url=
last_login=
```

`base_url` — `host:port` или `http(s)://host:port`, без пути и без логина в URL. `websocket_url` пустой значит «только /health». Без пути подставляется `/api/v1/ws`. `wss://` можно сохранить заранее. `last_login` обновляется по кнопке «Войти». Пароля в файле нет.

Если личного файла ещё нет, один раз читается **только для чтения** `hotel-desktop.ini` рядом с exe (каталог `hotel-desktop.exe`, не текущий каталог Qt Creator). Программа его не создаёт и не перезаписывает. Образец для копирования — `hotel-desktop.ini.example` (CMake кладёт его рядом с exe при сборке; само имя `.example` клиент не открывает). Как только личный файл появился — при «Сохранить» или при «Войти», который запоминает логин — файл рядом с exe больше не читается, даже если в личном файле не хватает ключа.

Нет ни личного файла, ни файла рядом с exe: встроенный адрес `http://127.0.0.1:8080`, WebSocket выключен.

## Окна

1. **Вход.** Логин, пароль, «Войти», «Настройки подключения». Логин запоминается, пароль нет. Сеть не блокирует интерфейс (`QNetworkAccessManager`, таймаут около 8 секунд на вход и 5 секунд на `/health`). Строка состояния — последний `/health` и WebSocket.
2. **После входа.** Имя, логин, `role_id`, `commands_allowed`, адрес сервера, живое состояние (опрос `/health` раз в 15 секунд; если WebSocket задан — ещё кадр hello). Центр пустой: «шахматка, бронирования». Токен только в памяти процесса, на экран не выводится.
3. **Сессия → Выход.** Токен стирается из памяти, снова окно входа. Метода logout в `hotel-api` пока нет: строка `nx_session` живёт до срока (12 часов) или пока её не уберут на сервере. Закрытие окна завершает процесс и тоже теряет токен.
4. Смена адреса сервера в настройках, пока сеанс открыт, тоже стирает токен и возвращает на вход. Смена только WebSocket сеанс не сбрасывает.

Сообщения входа:

| Ответ | Текст |
|-------|--------|
| сеть, таймаут, хост не найден, отказ | «Сервер недоступен…» или «Превышено время ожидания ответа сервера.» |
| 401 | «Неверный логин или пароль.» |
| 503 `database_not_configured` | «База не настроена…» |
| 503 `session_store_unavailable` | «Хранилище сессий недоступно…» |
| 503 `driver_not_loaded` | «Драйвер базы данных не загружен…» |
| 503 `database_unavailable` | «База данных недоступна…» |

Пустой `HOTEL_DSN` как раз даёт «База не настроена»: `/health` при этом `db.state=skipped`, а вход — 503.

## Smoke test

`hotel-desktop-login-smoke` checks address parsing, the INI rules, the main-window menu, and (when `HOTEL_API_BIN` is set) a real login against `hotel-api` with an empty DSN. The test points `XDG_CONFIG_HOME` at a temp directory so it does not write your settings. Offscreen is the default platform when `QT_QPA_PLATFORM` is unset.

```bash
cmake -S next -B /tmp/hotel-next-build -G Ninja
cmake --build /tmp/hotel-next-build --target hotel-desktop hotel-desktop-stub hotel-api hotel-desktop-login-smoke
HOTEL_API_BIN=/tmp/hotel-next-build/server/hotel-api \
  /tmp/hotel-next-build/desktopapp/hotel-desktop-login-smoke
```

`ctest --test-dir /tmp/hotel-next-build -R hotel-desktop` runs the same binary. Without `HOTEL_API_BIN` the live login case is skipped; the rest still run.
