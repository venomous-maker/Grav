# HTTP

`import "std/net"` (not included in `import "std"`) · namespace `std.net`

HTTP/1.1, over plain TCP or TLS (`https://`). This page covers the messages; the
server and the client have pages of their own:

- **[`HttpServer`](http_server.md)** routes requests to handler functions, with
  CORS, HTTPS, streaming responses and conditional requests.
- **[`HttpClient`](http_client.md)** sends requests over pooled keep-alive
  connections, follows redirects and verifies HTTPS certificates.
- **`HttpRequest`**, **`HttpResponse`**, **`Headers`**, **`Cookie`** and
  **`FormData`** are the pieces in between, defined here; URLs are
  [`Url`](url.md).

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
    app.start(0)                      // serve in the background, on any free port
    let base = app.baseUrl()

    let http = new HttpClient()
    print(http.get(base + "/hello/grav").body())
    print(http.get(base + "/search?q=json%20parser&page=2").body())
    let r = http.postJson(base + "/sum", Json.parse("{\"numbers\": [1, 2, 39]}"))
    print("${r.status} ${r.json()["total"].asInt()}")
    print(http.get(base + "/missing").status)
    http.close()
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

## Requests

`HttpRequest` is what a handler receives:

- **The line:** `method`, `target` (as sent), `path` (decoded, without the query),
  `param(name)`/`paramInt` for route captures, `query(name)`, `queryInt`,
  `queryAll(name)` for repeated fields, `queryParams()`.
- **Headers:** `header(name)`, and the common ones read for you: `contentType()`,
  `contentLength()`, `host()`, `origin()`, `userAgent()`, `accepts(mime)`,
  `bearerToken()`, `basicAuth()` (`[user, password]` or `null`), `cookie(name)`.
- **The body:** `body()`, `bodyBytes`, `json()`, `isJson()`, `form()` and
  `formValue(name)` (url-encoded or multipart), `file(name)` / `files()` /
  `parts()` for uploads (each a `FormPart` with `name`, `filename`,
  `contentType` and `data`).
- **The connection:** `ip()`, `remote`, `isSecure()` (TLS), `scheme`, `url()`.

## Responses

`HttpResponse` builders cover the usual answers: `text(s)`, `html(s)`,
`json(value)`, `json(status, value)`, `error(status, message)` (a JSON error),
`of(status, contentType, body)`, `status(code)`, `bytes(type, data)`,
`file(path)`, `created(json)`, `accepted()`, `noContent()`, `notModified()`,
`redirect(url[, code])`, `badRequest(msg)`, `unauthorized(realm)`,
`forbidden()`, `notFound()`, `methodNotAllowed(allow)`, `conflict(msg)`,
`unprocessable(msg)`, `tooManyRequests(retryAfter)`, `serverError(msg)` and
`unavailable(retryAfter)`. Chain changes onto them: `header(k, v)`,
`contentType(t)`, `cacheControl(v)`, `noCache()`, `etag(tag)`,
`attachment(filename)`, `setCookie(...)`, `clearCookie(name)`, `withStatus(code)`,
`withBody(text)`.

Reading one (on the client side): `status`, `ok()`, `isRedirect()`,
`isClientError()`, `isServerError()`, `body()`, `json()`, `getHeader(name)`,
`mediaType()`, `cookies()`, `elapsedMs`, and `ensureOk()` to throw on a
non-2xx status. `HttpResponse.reason(code)` knows every standard status.

## Streaming and server-sent events

`HttpResponse.stream(type, fn(w: ResponseWriter) { ... })` sends the body as it
is written (chunked encoding): `w.write(text)` goes out at once.
`HttpResponse.eventStream(fn(ev: EventStream) { ... })` is the same for
[server-sent events](https://html.spec.whatwg.org/multipage/server-sent-events.html):
`ev.send(event, data)`, `ev.data(text)`, `ev.sendWithId`, `ev.comment`, `ev.retry`.

## Headers, cookies and forms

- **`Headers`** keep their order; names ignore case and may repeat. `get`,
  `getAll`, `getInt`, `has`, `hasToken` (for comma lists like `Connection`),
  `set`, `add`, `setDefault`, `remove`, `keys`, `toMap`. A value containing a line
  break is refused with `ValueError`, so input placed in a header cannot add
  headers of its own. `Headers.param(value, key)` reads a `; key=value` parameter.
- **`Cookie`** builds a `Set-Cookie` header with its attributes:
  `new Cookie("sid", id).maxAge(3600).secure(true).withSameSite("Strict")`;
  `Cookie.parse(header)` reads one back.
- **`FormData`** collects fields and files (`add`, `addFile`) and turns into a
  multipart body; `FormData.parse(contentType, body)` splits one up.

```grav
import "std"
import "std/net"

fn main() {
    let app = new HttpServer()
    app.post("/upload", fn(req: HttpRequest) -> HttpResponse {
        let f = req.file("doc")
        return HttpResponse.text(req.formValue("title") + ": " + f.filename + " (${f.size()} bytes)")
            .setCookie(new Cookie("seen", "yes").maxAge(60))
    })
    app.start(0)
    let form = new FormData().add("title", "notes").addFile("doc", "a.txt", "text/plain", Bytes.fromString("hello"))
    let r = new HttpClient().postForm(app.baseUrl() + "/upload", form)
    print(r.body())
    print(r.getHeader("Set-Cookie"))
    print(r.cookies().get(0).maxAgeSeconds)
    app.stop()
}
```

Output:

```text
notes: a.txt (5 bytes)
seen=yes; Path=/; Max-Age=60; HttpOnly; SameSite=Lax
60
```

See also [`WebSocket`](websocket.md), which shares the server's port, and
[`examples/20_networking.grav`](../../../examples/20_networking.grav) for a REST
API and a chat room in one program.

## API

<!-- api:begin (generated by docs/std/gen-api.js from the library source; do not edit) -->

### class Headers

HTTP headers: ordered, case-insensitive names, repeatable.

**Constructors**

- `new Headers()`

**Static members**

- `static fn param(value: string, key: string) -> string` — A header value's main part and its `; key=value` parameters: "text/html; charset=utf-8" -> "text/html", {charset: "utf-8"} (keys lower case).
- `static fn mainValue(value: string) -> string` — The part of a header value before any ';' parameters, trimmed and lower case.

**Methods**

- `fn get(name: string) -> string` — The first value of `name`, or "".
- `fn getInt(name: string, fallback: int) -> int` — The first value of `name` as a number, or `fallback`.
- `fn getAll(name: string) -> Array<string>` — Every value of `name`, in order (e.g. each Set-Cookie line).
- `fn has(name: string) -> bool` — Whether a header named `name` is present (names ignore case).
- `fn hasToken(name: string, token: string) -> bool` — Whether `name` has `token` in its comma-separated value, ignoring case ("Connection: keep-alive, Upgrade" has "upgrade").
- `fn set(name: string, value: string) -> Headers` — Replaces every value of `name` with one value; returns these headers.
- `fn add(name: string, value: string) -> Headers` — Adds a value, keeping existing ones (e.g. several Set-Cookie headers).
- `fn setDefault(name: string, value: string) -> Headers` — Sets `name` only when it is not present yet.
- `fn addAll(other: Headers) -> Headers` — Adds every header line of `other`.
- `fn remove(name: string)` — Removes every header named `name`.
- `fn clear()` — Removes every header.
- `fn size() -> int` — The number of header lines.
- `fn isEmpty() -> bool` — Whether there are no headers.
- `fn nameAt(i: int) -> string` — The name of header line i.
- `fn valueAt(i: int) -> string` — The value of header line i.
- `fn keys() -> Array<string>` — The distinct names, in first-seen order.
- `fn toMap() -> HashMap<string, string>` — Name -> value, repeated names joined with ", " (lower-case names).
- `fn copy() -> Headers` — A copy of these headers.
- `fn forEach(f: fn(string, string))` — Calls f with each header's name and value, in order.
- `fn toString() -> string` — The headers as "Name: value" lines.

### class Cookie

A cookie for a Set-Cookie header, with its attributes. The defaults suit a
session cookie: Path=/, HttpOnly, SameSite=Lax, gone when the browser closes.

**Constructors**

- `new Cookie(name: string, value: string)`

**Static members**

- `static fn parse(header: string) -> Cookie` — A Set-Cookie header value read back into a Cookie (unknown attributes are ignored).

**Fields**

- `name: string` — The cookie's name.
- `value: string` — Its value (sent back as is; encode anything unusual).
- `path: string` — The URL path it applies to ("/" by default; "" leaves it out).
- `domain: string` — The domain it applies to ("" = only the host that set it).
- `maxAgeSeconds: int` — Seconds to keep it; -1 = until the browser closes, 0 = delete it now.
- `isSecure: bool` — Whether browsers send it over https only.
- `isHttpOnly: bool` — Whether page scripts are kept from reading it.
- `sameSite: string` — "Lax", "Strict", "None" or "" (unset).

**Methods**

- `fn maxAge(seconds: int) -> Cookie` — Keeps it for `seconds` (0 deletes it); returns this cookie.
- `fn secure(on: bool) -> Cookie` — Sends it over https only; returns this cookie.
- `fn httpOnly(on: bool) -> Cookie` — Hides it from page scripts (on by default); returns this cookie.
- `fn withPath(p: string) -> Cookie` — Limits it to URLs under `p`; returns this cookie.
- `fn withDomain(d: string) -> Cookie` — Shares it with the subdomains of `d`; returns this cookie.
- `fn withSameSite(mode: string) -> Cookie` — "Lax", "Strict" or "None" ("None" also needs secure(true) in browsers).
- `fn toHeader() -> string` — The Set-Cookie header value.
- `fn toString() -> string`

### class FormPart

One part of a multipart/form-data body: a field, or an uploaded file.

**Constructors**

- `new FormPart(name: string, filename: string, contentType: string, data: Bytes)`

**Fields**

- `name: string` — The form field's name.
- `filename: string` — The uploaded file's name ("" for an ordinary field).
- `contentType: string` — The part's Content-Type ("text/plain" when not given).
- `headers: Headers` — The part's own headers (Content-Disposition, Content-Type, ...).
- `data: Bytes` — The content.

**Methods**

- `fn isFile() -> bool` — Whether the part is a file upload.
- `fn text() -> string` — The content as text.
- `fn size() -> int` — The size in bytes.

### class FormData

The fields and files of a form, sent by HttpClient as multipart/form-data
(or url-encoded when there are no files).

**Constructors**

- `new FormData()`

**Static members**

- `static fn parse(contentType: string, body: Bytes) -> Array<FormPart>` — The parts of a multipart body, given its Content-Type header (which holds the boundary). ValueError when the body is not well formed.

**Fields**

- `parts: Array<FormPart>` — The fields and files, in order.
- `boundary: string` — The line that separates the parts in the body (random).

**Methods**

- `fn add(name: string, value: string) -> FormData` — Adds a text field; returns this form.
- `fn addFile(name: string, filename: string, contentType: string, data: Bytes) -> FormData` — Adds a file; returns this form.
- `fn hasFiles() -> bool` — Whether any part is a file.
- `fn contentType() -> string` — The Content-Type header for the multipart body (with its boundary).
- `fn body() -> Bytes` — The multipart/form-data body.
- `fn urlEncoded() -> string` — The text fields as application/x-www-form-urlencoded ("a=1&b=two").

### class HttpRequest

An incoming request (server side).

**Constructors**

- `new HttpRequest(method: string, target: string, version: string)`

**Fields**

- `method: string` — "GET", "POST", ... (upper case).
- `target: string                 // as sent: "/users/7?x=1"` — as sent: "/users/7?x=1"
- `path: string                   // decoded path without the query: "/users/7"` — decoded path without the query: "/users/7"
- `version: string` — "HTTP/1.1" or "HTTP/1.0".
- `headers: Headers` — The request headers.
- `bodyBytes: Bytes` — The body as sent.
- `remote: string                 // the client's address, "ip:port"` — the client's address, "ip:port"
- `scheme: string` — "https" for a TLS connection, else "http".

**Methods**

- `fn header(name: string) -> string` — The value of header `name` ("" when absent).
- `fn body() -> string` — The body as text.
- `fn json() -> JsonValue` — The body parsed as JSON (throws ValueError when it is not JSON).
- `fn param(name: string) -> string` — A route parameter: for the route "/users/:id", param("id").
- `fn paramInt(name: string, fallback: int) -> int` — A route parameter as a number, or `fallback` when it is not one.
- `fn setParam(name: string, value: string)` — Sets a path parameter (as a route pattern's `:name` would).
- `fn query(name: string) -> string` — A query-string value ("" when absent).
- `fn queryInt(name: string, fallback: int) -> int` — A query-string value as a number, or `fallback`.
- `fn queryAll(name: string) -> Array<string>` — Every value of a repeated query field ("?tag=a&tag=b").
- `fn hasQuery(name: string) -> bool` — Whether the query string has `name`.
- `fn queryParams() -> HashMap<string, string>` — Every query field (the last value of repeated names).
- `fn queryString() -> string` — The raw query string ("" when there is none).
- `fn contentType() -> string` — The Content-Type without parameters, lower case ("application/json").
- `fn contentLength() -> int` — The declared body length (-1 when not given).
- `fn isJson() -> bool` — Whether the body is JSON.
- `fn form() -> HashMap<string, string>` — Form fields: from an url-encoded body or the text fields of a multipart one.
- `fn formValue(name: string) -> string` — One form field ("" when absent).
- `fn isMultipart() -> bool` — Whether the body is multipart/form-data (a form with file uploads).
- `fn parts() -> Array<FormPart>` — The parts of a multipart/form-data body (empty for other bodies).
- `fn file(name: string) -> FormPart` — The uploaded file for form field `name`, or null.
- `fn files() -> Array<FormPart>` — Every uploaded file.
- `fn cookies() -> HashMap<string, string>` — The cookies the client sent (from the Cookie header).
- `fn cookie(name: string) -> string` — The value of cookie `name` ("" when the client did not send it).
- `fn ip() -> string` — The client's IP address (without the port).
- `fn host() -> string` — The Host header ("example.com:8080"), "" when absent.
- `fn isSecure() -> bool` — Whether the request came over TLS.
- `fn url() -> string` — The full URL the client asked for, from the Host header.
- `fn origin() -> string` — The Origin header (sent by browsers on cross-site requests), "" when absent.
- `fn userAgent() -> string` — The User-Agent header.
- `fn bearerToken() -> string` — The token of an "Authorization: Bearer <token>" header ("" when absent).
- `fn basicAuth() -> Array<string>` — [user, password] from an "Authorization: Basic" header, or null.
- `fn accepts(mime: string) -> bool` — Whether the client accepts `mime` ("application/json", "text/html"), from its Accept header (no header accepts everything).
- `fn keepAlive() -> bool` — The client wants the connection kept open after this request.

### class ResponseWriter

Writes a streamed response body (see HttpResponse.stream) as HTTP chunks.

**Constructors**

- `new ResponseWriter(conn: TcpStream)`

**Methods**

- `fn write(text: string)` — Sends text now (NetError once the client has gone away).
- `fn writeBytes(data: Bytes)` — Sends raw bytes now.
- `fn finish()` — Ends the body (done automatically when the stream function returns).
- `fn isOpen() -> bool` — Whether finish() has not been called.

### class EventStream

Server-sent events (text/event-stream) on a streamed response.

**Constructors**

- `new EventStream(out: ResponseWriter)`

**Methods**

- `fn send(event: string, data: string)` — Sends an event with this name and data (data may span lines).
- `fn data(text: string)` — Sends a message with no event name.
- `fn sendWithId(id: string, event: string, data: string)` — Sends an event with an id (clients resume from it with Last-Event-ID).
- `fn comment(text: string)` — Sends a comment line (keeps idle connections open through proxies).
- `fn retry(ms: int)` — Tells the client how long to wait before reconnecting.

### class HttpResponse

A response: built by server handlers, or returned by HttpClient.

**Constructors**

- `new HttpResponse(status: int)`

**Static members**

- `static fn text(body: string) -> HttpResponse` — A 200 response with a plain-text body.
- `static fn text(status: int, body: string) -> HttpResponse` — A plain-text response with the given status.
- `static fn html(body: string) -> HttpResponse` — A 200 response with an HTML body.
- `static fn json(value: JsonValue) -> HttpResponse` — A 200 response with a JSON body.
- `static fn json(status: int, value: JsonValue) -> HttpResponse` — A JSON response with the given status.
- `static fn of(status: int, contentType: string, body: string) -> HttpResponse` — A response with the given status, Content-Type and body.
- `static fn status(code: int) -> HttpResponse` — A response with the given status and its reason phrase as the body.
- `static fn error(code: int, message: string) -> HttpResponse` — A JSON error body {"error": message} with the given status.
- `static fn notFound() -> HttpResponse` — 404 Not Found.
- `static fn badRequest(message: string) -> HttpResponse` — 400 Bad Request with a message.
- `static fn unauthorized(realm: string) -> HttpResponse` — 401 Unauthorized, asking for Basic credentials for `realm`.
- `static fn forbidden() -> HttpResponse` — 403 Forbidden.
- `static fn methodNotAllowed(allow: string) -> HttpResponse` — 405 Method Not Allowed, listing the allowed methods ("GET, POST").
- `static fn conflict(message: string) -> HttpResponse` — 409 Conflict with a message.
- `static fn unprocessable(message: string) -> HttpResponse` — 422 Unprocessable Content with a message (valid syntax, invalid values).
- `static fn tooManyRequests(retryAfterSeconds: int) -> HttpResponse` — 429 Too Many Requests, with Retry-After.
- `static fn serverError(message: string) -> HttpResponse` — 500 Internal Server Error with a message.
- `static fn unavailable(retryAfterSeconds: int) -> HttpResponse` — 503 Service Unavailable, with Retry-After.
- `static fn redirect(location: string) -> HttpResponse` — 302 Found: a temporary redirect to `location`.
- `static fn redirect(location: string, code: int) -> HttpResponse` — A redirect with the given code (301, 302, 303, 307 or 308).
- `static fn created(value: JsonValue) -> HttpResponse` — 201 Created with a JSON body.
- `static fn accepted() -> HttpResponse` — 202 Accepted (the work goes on after the response).
- `static fn noContent() -> HttpResponse` — 204 No Content (no body).
- `static fn notModified() -> HttpResponse` — 304 Not Modified (no body).
- `static fn bytes(contentType: string, data: Bytes) -> HttpResponse` — A 200 response with raw bytes and the given Content-Type.
- `static fn file(path: string) -> HttpResponse` — A 200 response with the file at `path` (Content-Type from its extension), or 404 when it cannot be read.
- `static fn stream(contentType: string, write: fn(ResponseWriter)) -> HttpResponse` — A response whose body `write` produces piece by piece (sent with chunked encoding as it is written: large or slow bodies, progress, live data).
- `static fn eventStream(events: fn(EventStream)) -> HttpResponse` — Server-sent events: `events` runs with the stream open; the response ends when it returns (browsers read it with EventSource).
- `static fn reason(code: int) -> string` — The reason phrase for a status code ("Not Found" for 404).

**Fields**

- `status: int` — The status code (200, 404, ...).
- `headers: Headers` — The response headers.
- `bodyBytes: Bytes` — The body.
- `streamer: fn(ResponseWriter)` — For a streamed body (see stream): called to write it, chunk by chunk.
- `elapsedMs: long` — Milliseconds the request took (set by HttpClient).
- `wasCompressed: bool` — Whether HttpClient decompressed the body (it came gzip- or deflate-encoded).

**Methods**

- `fn header(name: string, value: string) -> HttpResponse` — Sets a header; returns this response.
- `fn contentType(value: string) -> HttpResponse` — Sets the Content-Type.
- `fn cacheControl(value: string) -> HttpResponse` — Sets Cache-Control ("no-store", "public, max-age=3600", ...).
- `fn noCache() -> HttpResponse` — Tells clients and proxies not to store the response.
- `fn etag(tag: string) -> HttpResponse` — Sets an ETag (quoted) for conditional requests (see HttpServer's If-None-Match handling).
- `fn attachment(filename: string) -> HttpResponse` — Asks the browser to save the body as `filename`.
- `fn setCookie(name: string, value: string) -> HttpResponse` — Adds a Set-Cookie header (Path=/, HttpOnly, SameSite=Lax) for a session cookie.
- `fn setCookie(name: string, value: string, maxAgeSeconds: int) -> HttpResponse` — Adds a cookie that lasts `maxAgeSeconds` (0 deletes it in the browser).
- `fn setCookie(cookie: Cookie) -> HttpResponse` — Adds a Set-Cookie header for `cookie` with its attributes.
- `fn clearCookie(name: string) -> HttpResponse` — Tells the browser to delete cookie `name`.
- `fn withStatus(code: int) -> HttpResponse` — Changes the status code; returns this response.
- `fn withBody(text: string) -> HttpResponse` — Replaces the body with text; returns this response.
- `fn ok() -> bool` — Whether the status is 2xx.
- `fn isRedirect() -> bool` — Whether the status is a redirect (3xx with a Location).
- `fn isClientError() -> bool` — Whether the status is 4xx.
- `fn isServerError() -> bool` — Whether the status is 5xx.
- `fn body() -> string` — The body as text.
- `fn json() -> JsonValue` — The body parsed as JSON (ValueError when it is not JSON).
- `fn getHeader(name: string) -> string` — The value of response header `name` ("" when absent).
- `fn mediaType() -> string` — The Content-Type without parameters, lower case.
- `fn cookies() -> Array<Cookie>` — The cookies set by this response (its Set-Cookie headers).
- `fn ensureOk() -> HttpResponse` — Throws NetError unless the status is 2xx; returns this response.
- `fn toString() -> string` — "HttpResponse(404 Not Found)".

### class Http

Reading and writing HTTP messages on a TcpStream (shared by server and client).

**Static members**

- `static fn maxLineBytes() -> int` — The longest request line or header line accepted (bytes).
- `static fn contentType(path: string) -> string` — The Content-Type for a file name, from its extension ("application/octet-stream" when unknown).
- `static fn readHeaders(conn: TcpStream, into: Headers)` — Headers up to the blank line, at most 200 lines. Throws NetError on malformed input.
- `static fn readBody(conn: TcpStream, headers: Headers, untilClose: bool, limit: int) -> Bytes` — The body described by `headers` (chunked or Content-Length, at most `limit` bytes). With `untilClose`, a body with neither is read until the connection closes.
- `static fn head(firstLine: string, headers: Headers) -> string` — The request or status line and headers as one block of text.
- `static fn headInto(out: Bytes, firstLine: string, headers: Headers)` — The request or status line and headers, appended to `out`.
- `static fn intText(n: long) -> string` — A number as text ("1427"), for header values.
- `static fn writeHead(conn: TcpStream, firstLine: string, headers: Headers)` — Writes a request or status line and the headers to the connection.
- `static fn writeMessage(conn: TcpStream, firstLine: string, headers: Headers, body: Bytes)` — Writes the head and a body in one go (one packet for small messages).
- `static fn isCompressible(contentType: string) -> bool` — Whether a Content-Type is worth compressing (text, JSON, JavaScript, XML, SVG, WASM).
- `static fn acceptsEncoding(acceptEncoding: string, coding: string) -> bool` — Whether an Accept-Encoding value allows `coding` ("gzip"), honouring q=0.
- `static fn date() -> string` — Today's date in HTTP format ("Tue, 15 Nov 1994 08:12:31 GMT").

<!-- api:end -->
