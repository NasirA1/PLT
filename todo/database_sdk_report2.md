# A database access library for Thunder: drivers, abstractions and OR mapping

*Exploration report, 2026-11-20. Nothing in the toolchain was changed for it. Every claim about
today's language was checked against the current toolchain (0.10.0); the layering proposed in
Part 3 exists as a program that compiles and runs: [`database_prototype.th`](database_prototype.th),
with an in-memory driver standing in for a native one.*

## Summary

1. **Mostly yes - the pieces are in place, with one compiler bug to fix first.** The C++ Native
   Interface (TNI) can talk to any C database API through integer handles, strings, `byte[]`
   and callbacks; objects, interfaces, generics, closures, `with`, `nothing` and the packaging
   of native libraries give everything an elegant library needs on the Thunder side. But
   **an interface cannot mention a struct type in its signatures today** (`func put(c: Cell)`
   inside an interface is "unknown type 'Cell'"): `collect_interfaces()` resolves signatures
   before `collect_structs()` has registered any struct name. That has to be fixed before
   the library can be designed around interfaces such as `Connection.query(...): Row[]`
   (Part 2.3). It is a small, contained fix.
2. **Recommended shape: three layers, each written in the language's own idiom.**
   *Drivers* are native modules with a cursor-style, scalar-only API over handles (prepare,
   bind, step, column) - which is not a TNI compromise but the shape of every C database API
   (SQLite, ODBC, libpq). Each driver comes with a small Thunder adapter implementing one
   `Driver` interface. *The database layer* (`import "database"`) is driver-agnostic pure
   Thunder: connections, statements, rows, transactions, always-bound parameters. *OR mapping*
   is a generic `Table<T>` that works by **convention**: an entity says its table, columns,
   values and how it is made from a row, and `Table<T>` does the rest - type-checked when
   `Table<User>` is instantiated, with no reflection, because Thunder's generics are checked
   per instantiation.
3. **First driver: SQLite, vendored.** Public domain, one C file, every platform, nothing to
   install, an in-memory database for deterministic golden tests and classrooms. **Second:
   ODBC**, built optionally like the SDL display, which reaches SQL Server, PostgreSQL, MySQL,
   Oracle and more through one module. Native PostgreSQL/MySQL modules can follow.
4. **Later, a compiler feature removes the boilerplate**: `@entity("users")` on a struct (and
   `@key`/`@column` on fields) makes the compiler write the convention's methods, exactly as
   `@mock` already writes methods today. Fully static, errors at compile time.
5. **Language gaps that limit elegance, not feasibility**: no NULL for scalar types, no
   heterogeneous values (a small tagged `SqlValue` struct covers it), no iterator protocol for
   `for ... in` (rows are materialised, or read through an explicit cursor), annotations not
   allowed on fields, and native calls block every task (a slow query freezes all of them -
   the concurrency report's phase 2 is the fix).

---

## Part 1 - What is already in place

### 1.1 The C++ Native Interface (`include/thunder/native.h`, ABI 1)

| Capability | Today | What it means for a driver |
| --- | --- | --- |
| Scalars | `int` (64-bit), `float`, `bool`, `string` (UTF-8 + length) | INTEGER, REAL, BOOLEAN and TEXT cells cross directly |
| Raw data | `byte[]` both ways (`THUNDER_BYTES`) | BLOB cells and parameters |
| Callbacks | `func(...)` parameters, called with `api->call` | a driver could *push* rows, progress or trace lines into Thunder |
| Handles | by convention: an `int` the C++ side maps to its object (sockets do this) | connections, statements and cursors are handles |
| Errors | `api->fail()` stops the program (R030) | for *programmer* errors only; database errors must come back as values (see 3.5) |
| Arrays, maps, tuples, structs | **cannot cross** (S053) | a result set is read cell by cell, a parameter list bound one by one |
| Names | a module's native functions are registered under their **qualified** name, once per language (`socket._send`, `ساکټ._ولېږه`) | one driver library serves the English and the Pashto SDK twins |
| Loading | once, before the first statement; next to the program, next to `thundervm`, `--native-path` | missing drivers are reported at once (R028/R029) |
| Packaging | `thunderc --package` copies the program's native libraries beside it (`src/core/package.cpp`) | a packaged program carries its driver |
| Building | one source file + `include/`, links nothing of Thunder's; optional modules are skipped quietly when a dependency is missing (`lib/display/CMakeLists.txt`, SDL) | the template for optional ODBC/PostgreSQL drivers |

The "no structs across the boundary" rule looks like a limit but fits perfectly: SQLite
(`sqlite3_prepare_v2 / bind_* / step / column_*`), ODBC (`SQLPrepare / SQLBindParameter /
SQLFetch / SQLGetData`) and libpq (`PQprepare / PQexecPrepared / PQgetvalue`) all expose
exactly a handle-based cursor over scalar cells. The sockets library
([`lib/native/sockets`](../lib/native/sockets/thunder_sockets.cpp) +
[`sockets.th`](../lib/thunder/en/sockets.th)) is the working precedent for the whole pattern:
a handle table in C++, private `_name` natives inside a module, an object wrapping the handle,
`close()` for `with`, `nothing` for failure and a module-level `error()`.

### 1.2 The language

| Feature | Use in a database library |
| --- | --- |
| Structs with constructors, methods, private `_members` | `Database`, `Statement`, `Row`, `Transaction`, `Table<T>` |
| Interfaces with dynamic dispatch | the `Driver` every driver adapter implements; mocks for tests |
| Mixins | shared behaviour across drivers (statement caching, tracing) |
| **Generics checked per instantiation** (templates) | `Table<T>` may call `item.columns()` on any `T` that has it - verified (Part 3.3) |
| A struct's default value (`var blank: T`) | stands in for a static factory: `blank.from_row(row)` |
| Closures and `map`/`filter`/`reduce` | mappers (`func(r: Row): User ...`), result post-processing, trace hooks |
| `with` + `close()` | connections, statements, cursors and transactions are resources |
| `nothing` for any struct or interface value | "no such row": `users.find(7) == nothing` |
| Enums | column types, isolation levels, driver capabilities |
| Modules, private members | `database`, one module per driver; internals hidden |
| `fail(message)` (0.10.0) | stops a program that misuses the library, with the library's own message |
| `@mock` writes methods at compile time (`SemanticAnalyzer::make_mock`) | the precedent for `@entity` (Part 3.4) |
| Tasks, locks, channels | a connection pool is a `Semaphore` + a list; but see 2.4 on blocking |
| Translate knows SDK names (0.10.0) | an English database program translates to Pashto and back |

---

## Part 2 - What is missing or in the way

### 2.1 Verified limits, in order of impact

| # | Limit | Effect on the library | Remedy |
| --- | --- | --- | --- |
| 1 | **Interfaces cannot name struct types in their signatures** (compiler bug) | no `interface Connection` with `query(...): Row[]`; drivers must be scalar-only (which they are anyway, but higher-level interfaces cannot exist) | fix: resolve interface signatures after struct names are registered (2.3) - **prerequisite** |
| 2 | No NULL for `int`, `float`, `string`, `bool` | a nullable column cannot map onto a plain field | `row.is_null("col")` + defaults for v1; a `Nullable<T>` struct; optional types later (2.2) |
| 3 | No heterogeneous values (no `any`, no tagged unions) | parameters and cells are of several types in one list | a small tagged struct `SqlValue { kind, i, f, s, b }` - works today (prototype) |
| 4 | No iterator protocol for `for ... in` | a result set cannot be streamed with `for row in cursor` | `query()` returns `Row[]`; a `Cursor` with `while c.next()` for large results; an iterator protocol later |
| 5 | Annotations only on top-level declarations and methods, from a fixed list (`@test`, `@mock`) | `@entity`/`@key`/`@column` need the parser and the list extended | Part 3.4 |
| 6 | Native calls block every task | a slow query stops every task, timers included | documented in v1; the concurrency report's phase 2 (native calls on a helper thread, the task waits) |
| 7 | No date/time or decimal types | DATE/TIMESTAMP/NUMERIC have no natural home | map to `string` (ISO 8601) or `int` (Unix time); NUMERIC to `string` to keep precision |
| 8 | A module's name is reserved for every variable and parameter in every importing program (S065) | a library module called `sql`, `db` or `row` would forbid the most natural variable names | name modules deliberately (`sqlval`, `database`), prefer objects to modules for user-facing API |
| 9 | Programs cannot declare a member named `int`, `float`, `string`, `bool` (built-in modules can: `random.int`) | `row.int("id")` is not available; `row.integer("id")` is | accept it, or allow type words after a dot in member declarations as they are in calls |
| 10 | No static methods; interfaces are not generic | `User.from_row(r)` and `interface Repository<T>` do not exist | the default-value prototype idiom; `Table<T>` is a struct |
| 11 | No exceptions; `with` cleanups do not run on a runtime error | a failed statement cannot unwind a transaction | `Transaction.close()` rolls back unless `commit()` was called; database errors are values (3.5) |

Items 2-11 make the library less elegant than it could be; none prevents it. Item 1 does
prevent the clean design, which is why it comes first.

### 2.2 NULL: the options

* **v1 (no language change):** a nullable column maps to the type's default, and
  `row.is_null(column)` tells the two apart. Entities declare their columns NOT NULL where
  they can. Honest and simple; enough for teaching.
* **A generic `Nullable<T>` struct** in the SDK: `nickname: Nullable<string>`, where the field
  itself is `nothing` for NULL (a struct value may be `nothing`) and `Nullable(value)` otherwise.
  Works today, reads clumsily.
* **Optional types in the language** (`nickname: string?`, `if nickname != nothing`): extends
  `nothing` from structs to every type. The right long-term answer, and useful far beyond
  databases (`string.to_int` could answer `int?` instead of stopping the program). A separate
  language decision.

### 2.3 The interface bug, precisely

`SemanticAnalyzer::analyze` runs pass 1 as `collect_enums(); collect_interfaces();
collect_structs(); collect_functions();` (`src/core/sema.cpp`, around line 55).
`collect_interfaces()` registers every interface name and then, in the same function,
`resolve_type`s every member's parameter and return types - before `collect_structs()` has
registered a single struct name. Enums and other interfaces resolve; structs and arrays of
structs do not:

```thunder
struct Cell
    v: int
interface Store
    func put(c: Cell): int        # semantic error [S031]: unknown type 'Cell'
```

The fix is to split interface collection: register names (before structs, so a struct may list
an interface declared below it, as now), then resolve member signatures after struct names
exist but before struct conformance (S074/S075) is checked. A few dozen lines and a test. It
affects every OOP design that passes data through an interface - the GUI SDK's
`Widget.draw(canvas: Canvas)` would hit it too.

### 2.4 Blocking

Every native call runs on the VM's one thread, so a query that takes a second stops every task
for a second. For scripts and teaching this does not matter; for a server-style program with
tasks it does. The concurrency report's phase 2 - a native function that runs on a helper
thread while only its task waits - is the general fix, and a database driver is its most
convincing use case. The driver API in 3.1 is compatible with it unchanged.

---

## Part 3 - The proposed design

### 3.1 Layer 1: driver modules (native, one per database)

Each driver is a native library plus a small SDK file. The library registers a fixed set of
cursor-style functions over integer handles, mirroring the C APIs:

| Function | Meaning |
| --- | --- |
| `_open(target: string): int` | connect (a file path for SQLite, a connection string for ODBC); 0 on failure |
| `_close(connection: int)` | |
| `_error(connection: int): string` | the last error, in the database's words |
| `_prepare(connection: int, sql: string): int` | a statement handle; 0 on failure |
| `_bind_int / _bind_float / _bind_text / _bind_bytes / _bind_null(statement, index, value)` | parameters, by position |
| `_step(statement: int): int` | 1 a row is ready, 0 done, -1 error |
| `_column_count`, `_column_name(i)`, `_column_kind(i)` | the shape of the result |
| `_column_int / _column_float / _column_text / _column_bytes(i)`, `_is_null(i)` | one cell |
| `_reset(statement)`, `_finish(statement)` | reuse or release a statement |
| `_changes(connection): int`, `_last_insert_id(connection): int` | after INSERT/UPDATE/DELETE |

The SDK side - `import "sqlite"` - declares those natives privately inside a module and offers
one struct, `SqliteDriver`, that implements the `Driver` interface by calling them. A second
database is a second pair (`import "odbc"`, `OdbcDriver`); a third party adds their own the same
way, without touching the core library. Programs import only the driver they use, so a program
never needs a library it does not load.

### 3.2 Layer 2: the database library (`import "database"`, pure Thunder)

```thunder
import "database"
import "sqlite"

with db = Database(SqliteDriver(":memory:"))
    db.execute("create table users (id integer primary key, name text not null)", [])
    db.execute("insert into users (name) values (?)", [sqlval.text("Ann")])
    println("new id: ", db.last_insert_id())

    for row in db.query("select id, name from users where name like ?", [sqlval.text("A%")])
        println(row.integer("id"), " ", row.text("name"))

    with tx = db.transaction()          # rolls back on close() unless committed
        db.execute("update users set name = ? where id = ?", [sqlval.text("Anna"), sqlval.integer(1)])
        tx.commit()

    var names = map(db.query("select name from users", []), func(r: Row): string r.text("name"))
```

| Object | Members (sketch) |
| --- | --- |
| `Database(driver)` | `execute(sql, params): int` (rows changed), `query(sql, params): Row[]`, `query_one(sql, params): Row` (`nothing` if none), `cursor(sql, params): Cursor`, `prepare(sql): Statement`, `transaction(): Transaction`, `last_insert_id()`, `error()`, `trace(func(sql: string))`, `close()` |
| `Statement` | `bind(index, SqlValue)`, `execute()`, `query()`, `reset()`, `close()` - for a statement run many times |
| `Row` | `integer(c)`, `real(c)`, `text(c)`, `bytes(c)`, `boolean(c)`, `is_null(c)`, by name or by position; `columns()` |
| `Cursor` | `next(): bool` and the same getters - streams a large result; `close()` |
| `Transaction` | `commit()`, `rollback()`, `close()` (rolls back if neither was called) |
| `SqlValue` + module `sqlval` | `sqlval.integer(5)`, `.real(2.5)`, `.text("x")`, `.bytes(data)`, `.null()` |

Design points:

* **Parameters are always bound, never pasted.** The API takes the SQL text and the values
  separately, so the obvious way to write a query is the safe one; SQL injection becomes a
  lesson (show the unsafe version, then the fix) rather than a habit. A later compiler warning
  could flag a query string built with `+` from non-literals.
* **Errors are values.** A constraint violation or a lost connection is not a bug in the
  program, so it must not stop it: `execute` answers -1 / `query` an empty array / `query_one`
  `nothing`, and `db.error()` says why - the sockets library's model, which students already
  know. A `Result<T>` struct (`ok`, `value`, `error`) is the alternative; see Decision 3.
  Misuse (a closed connection, a column that does not exist) is `fail(...)`.
* **Resources close themselves.** Connections, statements, cursors and transactions all have
  `close()`, so `with` releases them; a transaction that is not committed rolls back, which is
  the only safe behaviour in a language without exceptions.
* **Tracing.** `db.trace(func(sql: string) println(sql))` prints every statement with its
  parameters - visibility under the bonnet, in the project's spirit.

### 3.3 Layer 3: OR mapping by convention (no language change)

An entity is an ordinary struct that provides four methods; `Table<T>` is a generic repository
that uses them. Because generics are checked when instantiated, `Table<User>` is type-checked
against `User`'s methods where it is written - a missing `from_row` is a compile error there.

```thunder
struct User
    id: int
    name: string
    func table(): string
        return "users"
    func columns(): string[]
        return ["id", "name"]
    func values(): SqlValue[]
        return [sqlval.integer(id), sqlval.text(name)]
    func from_row(r: Row): User
        return User(id: r.integer("id"), name: r.text("name"))

var users: Table<User> = Table(db)
users.insert(User(id: 1, name: "Ann"))
var ann = users.find(1)                       # nothing if there is no such row
for u in users.where("name like ?", [sqlval.text("A%")])
    println(u.name)
```

`Table<T>`: `insert(item)`, `update(item)`, `delete(item)`, `find(key): T`, `all(): T[]`,
`where(condition, params): T[]`, `count()`, and a small fluent query builder
(`users.query().where("age > ?", ...).order_by("name").limit(10).all()`). Inside it, a
default-valued `var blank: T` stands in for the static factory the language does not have:
`blank.from_row(row)`. Both points - methods called on `T`, and the prototype idiom - are in
the running [`database_prototype.th`](database_prototype.th).

**An explicit mapper** is the escape hatch for a struct that cannot or should not carry the
methods (a type from another library, a view over a join): `Mapper<T>(table, columns,
to_values: func(T): SqlValue[], from_row: func(Row): T)` built from closures, accepted by
`Table<T>` as well.

Closures cannot be turned into SQL (there are no expression trees), so `where` takes SQL text
for the database and `filter(...)` stays available for in-memory refinement afterwards. That
line - SQL in the database, functions in Thunder - is worth teaching explicitly.

### 3.4 Later: `@entity` - the compiler writes the convention

```thunder
@entity("users")
struct User
    @key
    id: int
    @column("user_name")
    name: string
```

The compiler synthesises `table()`, `columns()`, `values()` and `from_row()` from the fields,
in the same pass and the same way `make_mock` synthesises a mock's methods today. What it needs:

* annotations on fields (the parser accepts them only before declarations and methods today);
* `entity`, `key` and `column` in `known_annotations`, with Pashto spellings;
* the mapping from field types to column kinds: `int`, `float`, `string`, `bool`, `byte[]`,
  enums (as `int`), and later optional types; any other field type is a compile error naming
  the field ("a field of type `Address` cannot be a column");
* `@key` tells `find`/`update`/`delete` which column identifies a row.

Everything stays static: no runtime reflection, no dynamic values, errors at compile time.
Runtime reflection (`fields_of(value)`, `set_field(value, name, ...)`) was considered and is
not recommended: it would need a dynamic value type in a statically typed teaching language,
and it moves mistakes from compile time to run time.

### 3.5 Pashto

The SDK files come in twins as always (`database.th`, `sqlite.th` in `lib/thunder/ps/`), each
driver library registers both languages' qualified names, and the 0.10.0 translator already
pairs SDK names through the twins, so an English database program translates to Pashto and
back. **SQL stays SQL**: it lives in strings, which the translator never touches, and table and
column names are the database's, not the program's. Pashto vocabulary for the objects
(`ډېټابېس`, `کتار`, `راکړه ورکړه` ...) is a draft for a native speaker, as always - and every
member name must avoid the S065 trap of 2.1 item 8.

---

## Part 4 - Drivers across platforms

| Driver | Reaches | Dependency | Build | Recommendation |
| --- | --- | --- | --- | --- |
| **SQLite** | SQLite files, `:memory:` | none if vendored: `sqlite3.c` + `sqlite3.h` (public domain, ~250k lines, one file) | always built, every platform | **first**: zero setup for students, deterministic golden tests, classroom-safe |
| **ODBC** | SQL Server, PostgreSQL, MySQL/MariaDB, Oracle, DB2, Access ... | Windows: built in (`odbc32`); Linux/macOS: unixODBC + the database's ODBC driver | optional, skipped quietly when unixODBC is missing (like SDL) | **second**: one module, every server database |
| PostgreSQL (libpq) | PostgreSQL | libpq | optional | later, if ODBC's setup proves too heavy for classrooms |
| MySQL/MariaDB (libmariadb) | MySQL, MariaDB | libmariadb (LGPL) | optional | later |

Vendoring SQLite is the one decision with weight: the toolchain is otherwise "the standard
library only". The amalgamation is self-contained C with no dependencies, compiles in well under
a minute, and lives under `lib/native/sqlite/` beside the other sample libraries - outside the
toolchain proper. The alternative, linking the system `libsqlite3`, works on Linux and macOS but
not on a bare Windows, which is half the users.

Tests: the C++ suite drives `thunder_sqlite` against `:memory:` (deterministic); the SDK's
`Driver` interface lets `@mock` stand in for a database in unit tests; golden samples use
`:memory:` so their output never depends on a file left behind.

---

## Part 5 - Roadmap and effort

| Phase | Content | Size |
| --- | --- | --- |
| 0 | Fix interface signatures that name structs (2.3), with tests | small |
| 1 | `thunder_sqlite` (vendored) + `sqlite.th` + `database.th` (Database, Statement, Row, Cursor, Transaction, SqlValue, Driver), docs in both languages, one sample in the picker and an advanced one, C++ and golden tests | medium |
| 2 | `Table<T>` by convention, explicit `Mapper<T>`, query builder, `Nullable<T>` | medium |
| 3 | `@entity` / `@key` / `@column` (field annotations, sema synthesis, diagnostics, Pashto) | medium |
| 4 | `thunder_odbc` (optional build); connection pool on `Semaphore`; non-blocking driver calls together with the concurrency report's phase 2 | medium-large |
| 5 (language) | optional types (`T?`), an iterator protocol for `for ... in`, perhaps static methods | separate decisions |

Phases 0-2 need no language change beyond the bug fix and already give a complete, teachable
library: SQL with bound parameters, transactions, and repositories over entity structs.

---

## Part 6 - Decisions for you

1. **Fix the interface bug first?** (Recommended - it blocks the clean design and affects every
   OOP library.)
2. **SQLite: vendor the amalgamation, or require the system library?** (Recommended: vendor.)
3. **Errors as values: the sockets model (`-1` / `nothing` + `db.error()`), or a `Result<T>`
   struct?** (Recommended: the sockets model - consistent with the SDK, simpler to teach.)
4. **OR mapping: convention (`Table<T>`) first, `@entity` later?** (Recommended.) Or go
   straight to `@entity`?
5. **NULL in v1: defaults + `is_null()`, or `Nullable<T>`, or wait for optional types?**
   (Recommended: defaults + `is_null()` now; optional types as their own language decision.)
6. **Result sets: materialised `Row[]` plus an explicit `Cursor`?** (Recommended; an iterator
   protocol later would make `for row in cursor` possible.)
7. **Breadth: ODBC next, or native PostgreSQL/MySQL?** (Recommended: ODBC.)
