# События WebSocket

Сокет: `ws://<ws_listen>/api/v1/ws`. Это не HTTP GET. Слушатель поднимается только если задан `ws_listen` / `HOTEL_WS_LISTEN`. Другой путь на этом порту закрывается.

Кадры — JSON-текст. Сервер не шлёт hello и не подписывает сокет, пока клиент не доказал сессию. Неверный токен или первый кадр не `auth` закрывает сокет кодом 1008 и причиной `unauthorized`. Молчащий сокет событий не получает. Токен в лог не пишется.

## Вход

Один из двух способов:

1. Первый текстовый кадр: `{"type":"auth","token":"<64 hex>"}`. Любой другой первый кадр — отказ.
2. Параметр запроса: `/api/v1/ws?token=<64 hex>`.

Токен тот же, что у `Authorization: Bearer`. Сервер считает SHA-256 и ищет `nx_session`, как REST: нет строки, отозван, срок вышел или пользователь не `active` — отказ.

После успеха сервер шлёт:

```json
{"type":"hello"}
```

## События после commit

Их шлют только подписанным сокетам. Это подсказка перечитать HTTP, не замена ответа POST/PATCH.

| `type` | Когда | Поля |
|--------|--------|------|
| `reservation.created` | `POST /api/v1/reservations` вернул 201 | `reservation_id`, `status_code`, `stay_id`, `room_id`, `state_code` |
| `reservation.updated` | `PATCH` вернул 200 и бронь не `canceled` | те же |
| `reservation.cancelled` | `PATCH` вернул 200 и `status_code` стал `canceled` | те же |
| `room.status_changed` | тот же `PATCH` вернул 200 и `state_code` проживания `in_house` или `checked_out` | `room_id`, `status_code` (`occupied` при `in_house`, `vacant_dirty` при `checked_out`) |

`hotel-desktop` после hello держит сокет. Обрыв — повтор через 1 с, затем 2, 4, 8, … не чаще чем раз в 30 с. После hello пауза снова 1 с. Кадры `reservation.*` и `room.status_changed` заново запрашивают шахматку и список броней. Без токена в памяти клиент сокет не открывает.
