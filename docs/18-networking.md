# 18. Networking, JSON, threads & processes

These libraries cover programs that talk to the outside world: JSON documents,
TCP sockets, HTTP servers and clients, WebSockets, OS threads, and running other
programs.

| Import | Gives you |
|--------|-----------|
| `import "std"` | `Json`/`JsonValue`, `Bytes`, `Thread` and friends, `Command` (plus everything else in the standard library) |
| `import "std/net"` | `TcpStream`, `TcpListener`, `HttpServer`, `HttpClient`, `WebSocket`, `Url` |
| `import "std/json"`, `"std/thread"`, `"std/command"`, `"std/bytes"`, `"std/net/tcp"`, … | one module at a time |

Networking is plain TCP: `https://` and `wss://` are not supported yet (no TLS).

## JSON

`Json.parse` turns text into a `JsonValue`. Lookups never fail: a missing key or
index gives a null value, so chains such as `doc["a"]["b"][0]` are safe. The `asX()`
methods throw `TypeError` when the value has another type, and `getString`,
`getInt` and the other `getX` methods take a fallback instead.

```grav
import "std"

fn main() {
    let doc = Json.parse("{\"name\": \"grav\", \"tags\": [\"fast\", \"small\"], \"stars\": 42}")
    print(doc["name"].asString())
    print(doc["tags"][1].asString())
    print(doc.getInt("stars", 0))
    print(doc["owner"]["login"].isNull())
    print(doc.path("tags.0").asString())

    let out = Json.object()
        .set("ok", true)
        .set("count", 3 as long)
        .set("items", Json.array().push("a").push(1.5))
    print(out.toJson())
    print(out.pretty())

    try {
        Json.parse("{\"a\": }")
    } catch (e: ValueError) {
        print(e.message())
    }
}
```

Output:

```text
grav
small
42
true
fast
{"ok":true,"count":3,"items":["a",1.5]}
{
  "ok": true,
  "count": 3,
  "items": [
    "a",
    1.5
  ]
}
JSON parse error at line 1, column 7: unexpected character
```

- Objects keep insertion order. `set` replaces an existing key in place, and
  `remove(key)` deletes one.
- Integers are kept exactly as 64-bit values (`asLong()`); other numbers are doubles.
- `==` compares structure (object key order does not matter).
- `Json.tryParse(text)` returns `null` instead of throwing, and `Json.quote(s)`
  escapes a string as a JSON literal.

## Threads

`async fn` (chapter 16) runs short tasks on a shared pool. A `Thread` is a
dedicated OS thread, for blocking work such as an accept loop or a background
worker. If an exception escapes a thread's body, it is kept and reported by
`join()`, which throws `StateError`, or by `joinResult()`, which returns the
message.

```grav
import "std"

fn main() {
    let total = new AtomicInt(0)
    let wg = new WaitGroup()
    for (i in 0..4) {
        wg.add(1)
        Thread.spawn(fn() {
            for (k in 0..1000) { total.increment() }
            wg.done()
        }).detach()
    }
    wg.wait()
    print("total: ${total.get()}")

    let failing = Thread.spawn("worker", fn() { throw new ValueError("disk full") })
    print("worker: " + failing.joinResult())

    let pool = new Semaphore(2)          // at most two at a time
    let t = Thread.spawn(fn() { pool.withPermit(fn() { print("inside the semaphore") }) })
    t.join()
}
```

Output:

```text
total: 4000
worker: ValueError: disk full
inside the semaphore
```

| Type | Use it for |
|------|------------|
| `Thread` | `spawn(body)`, `spawn(name, body)`, `join()`, `joinResult()`, `detach()`, `Thread.sleepMs`, `Thread.cpuCount()` |
| `WaitGroup` | wait until a group of threads finishes: `add(n)`, `done()`, `wait()`, `waitFor(ms)` |
| `CountDownLatch` | a gate that opens after `n` calls to `countDown()` |
| `Semaphore` | limit concurrency: `acquire()`, `release()`, `withPermit(body)` |
| `RWLock` | many readers or one writer: `withRead`, `withWrite` |
| `Once` | run an initializer exactly once: `once.run(body)` |
| `Mutex`, `AtomicInt`, `Channel<T>` | from chapter 16, also usable across threads |

Closures capture copies of local variables, so threads share state through objects
(an `AtomicInt`, a class guarded by a `Mutex`), not by assigning to captured locals.

## Running programs

`Command` starts a program directly, without a shell, so arguments are never
re-split or expanded. It captures stdout and stderr separately, can feed stdin,
and can enforce a timeout.

```grav
import "std"

fn main() {
    let r = new Command("sh").arg("-c").arg("echo out; echo err >&2; exit 3").run()
    print("code ${r.code}, stdout [${Str.trim(r.stdout)}], stderr [${Str.trim(r.stderr)}]")

    print(new Command("tr").arg("a-z").arg("A-Z").stdin("shout\n").output())

    let slow = new Command("sleep").arg("10").timeoutMs(50).run()
    print("timed out: ${slow.timedOut}")

    try {
        new Command("no-such-program").run()
    } catch (e: IOError) {
        print(e.message())
    }
}
```

Output:

```text
code 3, stdout [out], stderr [err]
SHOUT

timed out: true
cannot run 'no-such-program': No such file or directory
```

The builder also has `args(array)`, `cwd(dir)`, `env(key, value)`, `clearEnv()`
and `inheritOutput()`. `run()` returns a `CommandResult` (`code`, `stdout`,
`stderr`, `timedOut`, `ok()`). `output()` returns stdout and throws `IOError`
when the program fails. `spawn()` starts the program and returns a `Child` with
`pid()`, `isRunning()`, `kill()`, `signal(n)` and `wait()`. `Process.exec` and
`Process.run` (chapter 14) still run shell command lines.

## TCP

```grav
import "std"
import "std/net"

fn main() {
    let listener = TcpListener.bind("127.0.0.1", 0)     // port 0: any free port
    let server = Thread.spawn(fn() {
        let conn = listener.accept()
        conn.lines(fn(line: string) { conn.writeLine("echo: " + line) })
        conn.close()
    })

    let s = TcpStream.connect("127.0.0.1", listener.port())
    s.writeLine("hello")
    s.writeLine("")
    print(s.readLine())
    print("[" + s.readLine() + "]")
    s.shutdownWrite()                 // the server sees end of input
    print("[" + s.readLine() + "] eof: ${s.eof()}")
    s.close()
    server.join()
    listener.close()
}
```

Output:

```text
echo: hello
[echo: ]
[] eof: true
```

- `TcpStream.connect(host, port[, timeoutMs])` accepts names and IPv4/IPv6 addresses.
- Reading: `readLine()` returns `""` at the end and sets `eof()`, so a blank line
  can be told apart from end of input. Also `read(max)`, `readBytes(max)`,
  `readExactly(n)`, `readAll()`, and `lines(f)`.
- Writing: `write`, `writeLine`, `writeBytes`. Options: `setTimeoutMs(ms)` (an
  expired read or write throws `TimeoutError`) and `setNoDelay(on)`.
- Errors throw `NetError` (a subclass of `IOError`). `TcpListener.close()` wakes
  a thread that is blocked in `accept()` with a `NetError`.
- `Bytes` is the buffer type for binary data. It supports `appendByte`,
  `appendUint(v, width)` (big-endian), `readUint`, `slice`, `toHex`, `fromHex`
  and `[]`.

## HTTP

`HttpServer` routes requests by method and path: `:name` captures one segment and
a trailing `*` captures the rest. Each connection is served on its own thread with
keep-alive. If a handler throws, the client gets a 500 response, and unknown
paths get 404 or 405.

```grav
import "std"
import "std/net"

fn main() {
    let app = new HttpServer()
    app.get("/hello/:name", fn(req: HttpRequest) => HttpResponse.text("hello " + req.param("name")))
    app.get("/search", fn(req: HttpRequest) => HttpResponse.json(
        Json.object().set("q", req.query("q")).set("page", req.query("page"))))
    app.post("/sum", fn(req: HttpRequest) -> HttpResponse {
        let total: long = 0
        for (n in req.json()["numbers"].asArray()) { total += n.asLong() }
        return HttpResponse.json(Json.object().set("total", total))
    })
    app.start(0)                      // background thread, free port
    let base = "http://127.0.0.1:${app.port()}"

    let http = new HttpClient()
    print(http.get(base + "/hello/grav").body())
    print(http.get(base + "/search?q=json%20parser&page=2").body())
    let r = http.postJson(base + "/sum", Json.parse("{\"numbers\": [1, 2, 39]}"))
    print("${r.status} ${r.json()["total"].asInt()}")
    print(http.get(base + "/missing").status)
    app.stop()
}
```

Output:

```text
hello grav
{"q":"json parser","page":"2"}
200 42
404
```

- **Server:** register routes with `get`/`post`/`put`/`patch`/`delete`/`any`. `use(f)`
  adds middleware (return a response to answer early, or `null` to continue), and
  `logRequests(true)` prints one line per request. `listen(port)` blocks;
  `start(port)` serves in the background and `stop()` shuts it down.
- **Requests** (`HttpRequest`) have `method`, `path`, `param(name)`, `query(name)`,
  `header(name)`, `body()`, `json()` and `form()`.
- **Responses** (`HttpResponse`) are built with `text`, `html`, `json`, `of(status,
  type, body)`, `status(code)`, `notFound()`, `badRequest(msg)` and `redirect(url)`,
  then adjusted with `.header(k, v)` and `.withStatus(code)`.
- **Client:** `HttpClient` has `get`, `post`, `put`, `delete`, `postJson`,
  `postForm` and `request`, and follows redirects. Set `timeoutMs` and `headers`
  on it. `HttpClient.fetch(url)` is a one-line GET. Responses expose `status`,
  `ok()`, `body()`, `json()` and `getHeader(name)`.
- `Url.parse`, `Url.encode`, `Url.decode` and `Url.parseQuery` handle URLs.

## WebSockets

`WebSocket.route(server, path, handler)` serves WebSockets from an `HttpServer`,
on the same port as its HTTP routes. `WebSocket.connect(url)` opens a client.
Pings are answered automatically, fragmented messages are reassembled, and
`send` is safe to call from several threads.

```grav
import "std"
import "std/net"

fn main() {
    let app = new HttpServer()
    WebSocket.route(app, "/echo", fn(ws: WebSocket) {
        ws.messages(fn(m: WsMessage) {
            if (m.text() == "bye") { ws.close(4000, "see you") }
            else { ws.send("echo: " + m.text()) }
        })
    })
    app.start(0)

    let ws = WebSocket.connect("ws://127.0.0.1:${app.port()}/echo")
    ws.send("hello")
    print(ws.receive().text())
    ws.send("bye")
    print("closed: ${ws.receive() == null}, code ${ws.closeCode}, reason: ${ws.closeReason}")
    app.stop()
}
```

Output:

```text
echo: hello
closed: true, code 4000, reason: see you
```

`receive()` returns a `WsMessage` (`text()`, `data` as `Bytes`, `isText()`,
`isBinary()`), or `null` once the connection is closed. `messages(f)` loops until
then. Use `sendBytes` for binary frames, `ping()` to check the peer, and
`close(code, reason)` for the closing handshake.

See [`examples/20_networking.grav`](../examples/20_networking.grav) for a REST API
and a chat room in one program.
