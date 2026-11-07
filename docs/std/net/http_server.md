# HTTP server

`import "std/net"` (not included in `import "std"`) · namespace `std.net`

`HttpServer` answers HTTP/1.1 requests by routing them to handler functions.
Requests are served by a pool of worker threads, and idle keep-alive
connections wait in a poller rather than on a thread, so a few workers keep up
with thousands of clients. The same server speaks HTTPS when given a
[`TlsContext`](tls.md).

```grav
import "std"
import "std/net"

fn main() {
    let app = new HttpServer()
    app.get("/items/:id", fn(req: HttpRequest) => HttpResponse.json(
        Json.object().set("id", req.paramInt("id", 0) as long)))
    app.delete("/items/:id", fn(req: HttpRequest) => HttpResponse.noContent())
    app.cors(Cors.allowOrigins(["https://app.example"]))
    app.start(0)

    let http = new HttpClient()
    print(http.get(app.baseUrl() + "/items/7").body())
    let opts = http.options(app.baseUrl() + "/items/7")
    print("${opts.status} " + opts.getHeader("Allow"))
    print(http.post(app.baseUrl() + "/items/7", "", "").status)

    let pre = new Headers()
    pre.set("Origin", "https://app.example").set("Access-Control-Request-Method", "DELETE")
    let p = http.send("OPTIONS", app.baseUrl() + "/items/7", null, "", pre)
    print(p.getHeader("Access-Control-Allow-Origin"))
    http.close()
    app.stop()
}
```

Output:

```text
{"id":7}
204 GET, HEAD, DELETE, OPTIONS
405
https://app.example
```

## Routes

- `get`, `post`, `put`, `patch`, `delete`, `head`, `options` and `any` add a
  handler for a path. `:name` captures one segment (`req.param("name")`), a
  trailing `*` the rest (`req.param("*")`).
- A GET route also answers HEAD (without sending the body). OPTIONS is answered
  from the routes with an `Allow` header; a known path with the wrong method gets
  405 with `Allow`; an unknown path gets 404, or what `onNotFound(handler)` returns.
- A handler that returns `null` answers 204. One that throws answers 500, or what
  `onError(fn(req, why) => ...)` returns. Each request is also a panic recovery
  point: a panicking handler answers 500, the panic is logged, and the server
  keeps serving (`withPanicRecovery(false)` turns that off).
- `use(f)` runs before routing (return a response to answer early, `null` to
  continue); `after(f)` runs after the handler and may change the response.
- `serveDir("/static", "public")` serves a folder, with ETags and ranges.
- `upgrade(path, handler)` hands the connection over for other protocols
  ([`WebSocket.route`](websocket.md) uses it).

## Browsers and caches

- **CORS:** `cors(Cors.allowAll())` lets pages on any site call the server;
  `Cors.allowOrigins([...])` lists the allowed ones, with `allowCredentials`,
  `allowMethods`, `allowHeaderNames`, `exposeHeaderNames` and `maxAge`. Preflight
  OPTIONS requests are answered automatically.
- **Conditional requests:** a response with an `etag(...)` is answered
  `304 Not Modified` when the client already has that version (`If-None-Match`).
- **Ranges:** a `Range: bytes=a-b` request for a tagged response gets
  `206 Partial Content` (or 416), so downloads can resume and media can seek.
- **Streaming:** `HttpResponse.stream` and `HttpResponse.eventStream` (server-sent
  events) are sent with chunked encoding as they are written.
- **Compression:** `compress(true)` gzips text, JSON, JavaScript, XML, SVG and
  WASM responses of 1 KiB or more (`compressMinBytes`) for clients that send
  `Accept-Encoding: gzip`, with `Vary: Accept-Encoding` and a weak ETag. Ranges
  and streams are sent uncompressed, and so are images and archives, which are
  compressed already.

## Running

- `listen(port)` serves until `stop()`; `start(port)` serves in the background
  (port 0 picks a free one; `port()` and `baseUrl()` say which).
- HTTPS: `listenTls(port, ctx)` / `startTls(port, ctx)` with
  `TlsContext.server(certFile, keyFile)`, or `TlsContext.selfSigned("localhost")`
  while developing. `isTls()` tells which kind is running.
- `stop()` stops accepting; `stopAndWait(ms)` also waits for open connections.
  `activeConnections()` and `requestsServed()` report the load.
- Limits: `maxBodyBytes` (413 above it), `maxConnections` (503 above it),
  `idleTimeoutMs` for idle keep-alive connections, `maxRequestsPerConnection`.
  Header lines longer than 16 KiB are refused with 431.
- `logRequests(true)` prints one line per request with its status and time.
- Connections: `workers` sets the number of worker threads (default 4 per CPU,
  at least 16). A worker reads one request, answers it, serves any pipelined
  requests that came with it, and hands the connection back to the poller.
  WebSocket and other upgraded connections and streamed responses
  (`HttpResponse.stream`, `eventStream`) get threads of their own, so they never
  hold a worker. Handlers that block for long (on other servers, say) need more
  workers, and so does a server whose clients send requests slowly, since a
  partly arrived request keeps its worker until it is complete or
  `idleTimeoutMs` passes. Where readiness polling is not available (outside
  Linux), each connection gets its own thread instead.

## API

<!-- api:begin (generated by docs/std/gen-api.js from the library source; do not edit) -->

### class Dispatch

The outcome of dispatching one request (filled in inside a recovery point).

**Constructors**

- `new Dispatch()`

**Fields**

- `res: HttpResponse` — The response to send (null after an upgrade).
- `upgraded: bool` — Whether an upgrade handler took the connection.
- `upgrade: Route` — The upgrade route that wants the connection (run by the caller).

### class PoolConn

A connection in the worker pool: idle in the poller, or being served.

**Constructors**

- `new PoolConn(stream: TcpStream)`

**Fields**

- `stream: TcpStream` — The connection.
- `fd: int` — Its socket descriptor (the poller's key for it).
- `lastActive: long` — When it last finished a request (monotonic ms), for the idle timeout.
- `busy: bool` — Whether a worker has it now.
- `requests: int` — Requests served on it so far.
- `handshaken: bool` — Whether its TLS handshake is done.

### class Route

One route: method + path pattern -> handler.

**Constructors**

- `new Route(method: string, pattern: string)`

**Static members**

- `static fn split(path: string) -> Array<string>` — The non-empty segments of a path ("/a/b/" gives "a", "b").

**Fields**

- `method: string` — The method it answers ("*" for any).
- `parts: Array<string>` — The pattern's path segments.
- `handler: fn(HttpRequest) -> HttpResponse` — The handler of an ordinary route.
- `upgrader: fn(HttpRequest, TcpStream)` — The handler of an upgrade route (gets the connection).
- `isUpgrade: bool` — Whether this is an upgrade route.

**Methods**

- `fn matches(segs: Array<string>, req: HttpRequest) -> bool` — Does `segs` match? Captures go into `req`.
- `fn allows(method: string) -> bool` — Whether this route answers `method` (a GET route also answers HEAD).

### class Cors

Cross-origin resource sharing: which other sites' pages may call this server
from a browser. HttpServer.cors(...) answers preflight OPTIONS requests and
adds the Access-Control headers to responses.

**Constructors**

- `new Cors()`

**Static members**

- `static fn allowAll() -> Cors` — Any origin, without credentials (a public API).
- `static fn allowOrigins(list: Array<string>) -> Cors` — Only these origins.
- `static fn isPreflight(req: HttpRequest) -> bool` — Whether `req` is a CORS preflight (OPTIONS with Access-Control-Request-Method).

**Fields**

- `origins: Array<string>` — Allowed origins ("https://app.example.com"); "*" allows any.
- `methods: string` — The methods allowed in preflight answers.
- `allowHeaders: string` — Request headers allowed ("" = whatever the preflight asks for).
- `exposeHeaders: string` — Response headers scripts may read.
- `credentials: bool` — Whether browsers may send cookies and credentials (needs explicit origins).
- `maxAgeSeconds: int` — How long browsers may cache a preflight answer (seconds).

**Methods**

- `fn allowOrigin(origin: string) -> Cors` — Adds an allowed origin ("*" for any); returns this policy.
- `fn allowMethods(list: string) -> Cors` — The allowed methods ("GET, POST"); returns this policy.
- `fn allowHeaderNames(list: string) -> Cors` — Request headers browsers may send ("Content-Type, Authorization"); by default whatever a preflight asks for is allowed.
- `fn exposeHeaderNames(list: string) -> Cors` — Response headers scripts may read ("X-Total-Count").
- `fn allowCredentials(on: bool) -> Cors` — Lets browsers send cookies; returns this policy.
- `fn maxAge(seconds: int) -> Cors` — How long a preflight answer may be cached; returns this policy.
- `fn allows(origin: string) -> bool` — Whether a request from `origin` is allowed.
- `fn preflight(req: HttpRequest) -> HttpResponse` — The answer to a preflight request.
- `fn decorate(req: HttpRequest, res: HttpResponse)` — Adds the response headers for an allowed origin.

### class HttpServer

An HTTP/1.1 server with a router.

**Constructors**

- `new HttpServer()`

**Fields**

- `workers: int` — Threads that serve requests (0: 4 per CPU, at least 16). Idle connections wait in a poller, not on a thread, so a few workers serve thousands of keep-alive clients; long handlers need more of them.
- `maxBodyBytes: int` — Largest request body accepted (bytes); larger ones get 413.
- `idleTimeoutMs: int` — Idle keep-alive connections are closed after this many milliseconds.
- `maxConnections: int` — At most this many connections at once; more are answered 503 and closed.
- `maxRequestsPerConnection: int` — Requests served on one connection before it is closed (0 = no limit).
- `compressMinBytes: int` — Responses at least this large (bytes) are gzip-compressed for clients that accept it; 0 turns compression off (see compress).
- `recoverPanics: bool` — A panic in a handler (or middleware, or WebSocket handler) is recovered: the client gets a 500, the panic is logged, and the server keeps running. false: panics on this server's threads stop the program. Panic.setRecovery(false) turns recovery off everywhere.

**Methods**

- `fn route(method: string, pattern: string, handler: fn(HttpRequest) -> HttpResponse) -> HttpServer` — Adds a handler for requests with this method whose path matches `pattern` (`:name` captures a segment); returns this server.
- `fn get(pattern: string, h: fn(HttpRequest) -> HttpResponse) -> HttpServer` — Handles GET (and HEAD) requests matching `pattern`; returns this server.
- `fn post(pattern: string, h: fn(HttpRequest) -> HttpResponse) -> HttpServer` — Handles POST requests matching `pattern`; returns this server.
- `fn put(pattern: string, h: fn(HttpRequest) -> HttpResponse) -> HttpServer` — Handles PUT requests matching `pattern`; returns this server.
- `fn patch(pattern: string, h: fn(HttpRequest) -> HttpResponse) -> HttpServer` — Handles PATCH requests matching `pattern`; returns this server.
- `fn delete(pattern: string, h: fn(HttpRequest) -> HttpResponse) -> HttpServer` — Handles DELETE requests matching `pattern`; returns this server.
- `fn head(pattern: string, h: fn(HttpRequest) -> HttpResponse) -> HttpServer` — Handles HEAD requests matching `pattern` itself (else the GET route answers them).
- `fn options(pattern: string, h: fn(HttpRequest) -> HttpResponse) -> HttpServer` — Handles OPTIONS requests matching `pattern` itself (else they are answered from the routes, with an Allow header).
- `fn any(pattern: string, h: fn(HttpRequest) -> HttpResponse) -> HttpServer` — Any method.
- `fn upgrade(pattern: string, handler: fn(HttpRequest, TcpStream)) -> HttpServer` — Hand the raw connection to `handler` when a request for `pattern` asks to upgrade the protocol (lib/net/websocket.grav uses this).
- `fn use(f: fn(HttpRequest) -> HttpResponse) -> HttpServer` — Runs before routing; return a response to answer immediately, or null to continue.
- `fn after(f: fn(HttpRequest, HttpResponse)) -> HttpServer` — Runs after a handler, and may change its response (add headers, log, ...).
- `fn onNotFound(h: fn(HttpRequest) -> HttpResponse) -> HttpServer` — Answers requests no route matches (instead of a plain 404).
- `fn onError(h: fn(HttpRequest, string) -> HttpResponse) -> HttpServer` — Builds the response when a handler throws (gets the error's description).
- `fn cors(policy: Cors) -> HttpServer` — Applies a CORS policy to every route; returns this server.
- `fn serveDir(prefix: string, dir: string) -> HttpServer` — Serves the files under `dir` at URLs starting with `prefix` ("/static" maps /static/css/site.css to dir/css/site.css; a folder serves its index.html). Paths that try to leave `dir` (with "..") get 404. Files get an ETag, so repeat visits are answered 304 Not Modified, and Range requests work.
- `fn compress(on: bool) -> HttpServer` — gzip-compresses text, JSON and other compressible responses of 1 KiB or more for clients that send Accept-Encoding: gzip; returns this server.
- `fn logRequests(on: bool) -> HttpServer` — Print one line per request ("GET /path -> 200 (1.2 ms)").
- `fn withPanicRecovery(on: bool) -> HttpServer` — Whether a panic in a handler is answered with a 500 instead of stopping the server (on by default).
- `fn listen(host: string, port: int)` — Serve on host:port, blocking until stop().
- `fn listen(port: int)` — Serves on every interface at `port`, blocking until stop().
- `fn listenTls(port: int, ctx: TlsContext)` — Serves https on every interface at `port` with `ctx`'s certificate, blocking.
- `fn listenTls(host: string, port: int, ctx: TlsContext)` — Serves https on host:port, blocking until stop().
- `fn start(host: string, port: int) -> HttpServer` — Serve on a background thread and return at once (port 0 picks a free port).
- `fn start(port: int) -> HttpServer` — Serves on 127.0.0.1:port on a background thread and returns at once (port 0 picks a free port).
- `fn startTls(port: int, ctx: TlsContext) -> HttpServer` — Serves https on 127.0.0.1:port in the background.
- `fn startTls(host: string, port: int, ctx: TlsContext) -> HttpServer` — Serves https on host:port in the background.
- `fn port() -> int` — The port being served.
- `fn isTls() -> bool` — Whether this server uses TLS.
- `fn baseUrl() -> string` — "http://127.0.0.1:8080" (or https) for the address being served.
- `fn activeConnections() -> int` — Connections open now.
- `fn requestsServed() -> long` — Requests answered so far.
- `fn stop()` — Stop accepting connections; requests being served are finished first (except when a handler calls this: then it returns at once, and the server winds down after that handler's request).
- `fn stopAndWait(ms: int) -> bool` — Stops, then waits up to `ms` for open connections to finish; returns whether they all did.

<!-- api:end -->
