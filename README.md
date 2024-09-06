# Battery Monitor

The goal of these set of programs is to have a
central server storing and serving the battery
percentage of all registered devices to know when
one has low battery and monitor their progress

## The tools

### Server (written in C)

Goals of server

- Serve dashboard website
- Listen to put request
- Listen to get request

```http
PUT /update
Content-Type: json
Body:
{
    id: "iPad 2.0",
    battery: 86.5,
    time: 123456789465
}
```

```http
GET /<id>
Content-Type: json
Body:
{
    id: "iPad 2.0",
    battery: 86.5,
    time: 123456789465
}
```

## Dashboard (HTML served by the Server)

## Shortcut  (client "updater")

