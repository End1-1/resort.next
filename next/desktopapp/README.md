# next/desktopapp

Qt/C++ pieces of the desktop that talk to `hotel-api` over HTTP. CMake only.

`hotel-desktop-stub` is a headless stand-in, not the reception UI. It links Qt Core and Network. It does **not** link Qt Sql and it does not open MariaDB. `GET /health` is the seam the future widgets will use: build a request, send it, show the JSON or the HTTP error. Business rules stay in `next/server`.

`Resort/` is unchanged and still writes SQL. Moving a screen (rack, reservation, folio) onto this client is a later phase, behind a flag, after the API actually implements that call.

The web client (`next/webport`) waits until this desktop app is real. It is not scaffolded here.

## Build

Same Qt 6 modules as the server for Core and Network (HttpServer is not required here):

```bash
cmake -S next -B /tmp/hotel-next-build -G Ninja
cmake --build /tmp/hotel-next-build --target hotel-desktop-stub
```

## Run

Start `hotel-api` first (`../server/README.md`), then:

```bash
HOTEL_API_BASE=http://127.0.0.1:8080 /tmp/hotel-next-build/desktopapp/hotel-desktop-stub
```

`HOTEL_API_BASE` is an origin with no path. Default `http://127.0.0.1:8080`. The program prints the `/health` body and exits 0 on HTTP 200. There is no database password in this program.
