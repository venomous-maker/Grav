# bank_server — a banking API in Grav

A small bank as a multi-file Grav project: customers, checking and savings
accounts, deposits, withdrawals with overdraft limits, atomic transfers, interest,
statements, and a JSON REST API with token auth.

```bash
cd examples/apps/bank_server
grav run                                  # self-test: a scripted API session, then stops
grav run -- --serve 8080 --data bank.json # a real server, saved to bank.json
grav test tests/bank_test.grav            # unit tests of the business rules
grav check && grav fmt --check            # type-check and format check
```

## Layout

| Layer | Files | Namespace | Responsibility |
|-------|-------|-----------|----------------|
| Entities | `src/entities/{money,customer,account,transaction}.grav` | `bank.entities` | the data: `Customer`, `Account` (kind, status, balance, overdraft), `Transaction`; `Money` keeps amounts as whole cents |
| Repository | `src/repository/store.grav` | `bank.repository` | in-memory storage, ids, JSON persistence (write-then-rename) |
| Services | `src/services/{errors,bank_service}.grav` | `bank.services` | the rules: validation, balances, transfers, interest; returns `Result<T, BankError>` |
| HTTP | `src/http/api.grav` | `bank.http` | JSON responses, error mapping, body parsing, auth middleware (`Gate`) |
| Controllers | `src/controllers/*_controller.grav` | `bank.controllers` | one class per resource: parse the request, call the service, shape the response |
| Routes | `src/routes.grav` | `bank` | the route table: endpoint → controller method |
| App | `src/app.grav`, `src/main.grav` | `bank` | wiring (store → service → controllers → routes) and the entry point |

`grav.toml` sets `paths = ["src"]`, so every module imports others by their path
from `src/` (`import "services/bank_service"`), and each layer brings in the one
below with `use bank.entities.*` and similar.

`BankError` is a data enum (`NotFound`, `Invalid`, `InsufficientFunds`, `NotActive`,
`Conflict`). Each variant knows its HTTP status, so controllers just `match` on the
service's `Result` and return `Api.error(e)`.

## API

Every `/api` route except `/api/health` needs `Authorization: Bearer <token>`
(`BANK_TOKEN`, default `dev-token`). Amounts are sent as `"12.50"` or `12.5`,
and returned as formatted strings (`"1,250.00"`). Errors look like
`{"error": {"code": "insufficient_funds", "message": "..."}}`.

| Method | Path | Body | Result |
|--------|------|------|--------|
| GET | `/api/health` | | status and request count (no token needed) |
| POST | `/api/customers` | `{"name", "email"}` | 201 customer; 409 if the email exists |
| GET | `/api/customers` | | all customers |
| GET | `/api/customers/:id` | | the customer with their accounts |
| POST | `/api/accounts` | `{"customerId", "kind": "checking" \| "savings"}` | 201 account (`ACC-000001`, …) |
| GET | `/api/accounts/:number` | | balance and available amount |
| GET | `/api/accounts/:number/transactions` | `?kind=deposit` | the statement |
| POST | `/api/accounts/:number/deposit` | `{"amount", "memo"?}` | the account |
| POST | `/api/accounts/:number/withdraw` | `{"amount", "memo"?}` | 409 past the available amount |
| POST | `/api/accounts/:number/freeze` / `unfreeze` / `close` | | frozen accounts refuse money; closing needs a zero balance |
| POST | `/api/transfers` | `{"from", "to", "amount", "memo"?}` | 201 receipt; both sides or neither |
| POST | `/api/admin/interest` | `{"basisPoints": 125}` | pays 1.25% on active savings accounts |
| GET | `/api/reports/summary` | | counts and total deposits |

```bash
T='Authorization: Bearer dev-token'
curl -H "$T" -d '{"name":"Ada Lovelace","email":"ada@example.com"}' localhost:8080/api/customers
curl -H "$T" -d '{"customerId":1,"kind":"checking"}' localhost:8080/api/accounts
curl -H "$T" -d '{"amount":"250.00","memo":"salary"}' localhost:8080/api/accounts/ACC-000001/deposit
curl -H "$T" localhost:8080/api/accounts/ACC-000001/transactions
```

## Rules

- Checking accounts may go 500.00 below zero. Savings accounts never go below zero
  and earn interest, rounded down to the cent.
- Each operation holds the store's lock from validation to save, so concurrent
  transfers never lose money. The self-test runs 4 clients × 25 transfers and
  checks the total, and the unit tests run 8 threads.
- Amounts must be positive, have at most two decimals, and be at most 10,000,000.00.
