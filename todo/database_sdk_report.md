# A database library for Thunder: what the toolchain already carries, and what to build

*Exploration report, 2026-11-20. Nothing in the toolchain was changed for it. Every shape quoted
in Part 1 was compiled and run against the current toolchain (0.10.0); every limit in Part 2 was
found by trying it, not by reading.*

## The short answer

**Yes for the access library, and almost for the mapping.** Objects, interfaces, generics,
closures, `with`, enums, `byte[]` and the native interface are between them enough to write a
database library that reads well and takes more than one driver - and the shape the native
interface forces (single values in, single values out) turns out to be exactly the shape a
driver contract wants. Four things were tried and work today, unchanged:

| What a database library needs | How the language already gives it |
| --- | --- |
| A column value of *any* type | A struct with an enum tag (`Value`), because the language has no dynamic type |
| A row read by column name | `{string: Value}` - a map is homogeneous, and `Value` makes it so |
| Result rows mapped to a struct | A generic struct whose method takes a function value: `Rows<T>.all(read: func(Row): T): T[]` |
| One library, several drivers | `interface Driver`, implemented once per database, dispatched at run time |
| A transaction that rolls back unless it is committed | `with`: the guard's `close()` rolls back when `commit()` was never called |

The gap is **object-relational mapping of the declarative kind**. Thunder has no reflection over
a struct's fields and no annotations of its own, so a `@table`/`@column` library in the style of
Hibernate or Entity Framework cannot be written in Thunder today. What *can* be written - and
what suits a teaching language better - is a mapping layer the program feeds two small functions
(row → object, object → values). Part 4 lays out both, and says exactly which two small language
additions would unlock the declarative kind if it is ever wanted.

---

## Part 1 - What is already there (verified)

### 1.1 The native interface is handle-shaped, and so is every database API

`include/thunder/native.h` lets exactly these cross the boundary: `int`, `float`, `bool`,
`string`, `byte[]`, and a Thunder function value **as a parameter only**. Arrays, maps, tuples
and structs cannot cross. That reads like a restriction until you look at SQLite's, ODBC's and
libpq's C APIs: they are all "an integer (or pointer) handle plus one scalar at a time". The
sockets module (`lib/native/sockets/thunder_sockets.cpp` + `lib/thunder/<lang>/sockets.th`) is
the finished template: a C++ table of open handles, a Thunder module of `native func _x(handle,
...)` declarations, and a struct that wraps the handle so programs never see it.

`byte[]` crossing the boundary means **BLOBs work** with no extra machinery.

### 1.2 Everything the design needs compiles today

This program compiles and runs as written (it is the skeleton of Part 3, with the driver faked):

```thunder
enum Kind
    null_value
    integer
    real
    text

struct Value                        # a tagged value: the language has no "any"
    kind: Kind
    i: int
    f: float
    s: string

struct Row
    cells: {string: Value}          # a map is homogeneous - Value makes the row fit in one

struct Rows<T>                      # a generic result set ...
    _rows: Row[]
    func all(read: func(Row): T): T[]   # ... mapped by a function value
        var out: T[] = []
        for row in _rows
            push(out, read(row))
        return out

interface Driver                    # one library, several databases
    func name(): string
    func open(where: string): bool

struct Transaction                  # "with" rolls it back unless it was committed
    _done: bool
    func commit()
        _done = true
    func close()
        if not _done
            rollback()
```

`inspect(p)` already prints a struct's fields **with their types** at run time, which is worth
knowing for Part 4: the virtual machine has the layout; the language simply does not hand it to
a program yet.

### 1.3 The rest of the shelf that a database library would lean on

* **`with`** closes a connection, a statement and a result set, however the block ends.
* **`nothing`** is the sockets precedent for "this failed, and the program may handle it".
* **`fail(message)`** (0.10.0) is the precedent for "the program did something wrong" - using a
  connection after closing it.
* **`import "testing"`** gives `@test` suites with a fixture built by the constructor and torn
  down by `close()`: exactly a "open a temporary database, drop it afterwards" fixture.
* **`import "concurrency"`** gives a `Channel` whose `has_next()` idiom a lazy row cursor should
  copy, so the two libraries read alike.
* **Closures** carry the mapper: `rows.all(person_of)` today, and
  `rows.all(func(r) -> Person(...))` once the deduced single-expression lambda lands - the
  database library is the best argument for finishing that.

---

## Part 2 - The four limits that shape the design (each one tried)

1. **There is no `self`.** A method cannot return its own receiver:
   `return self` is `S001: 'self' is not declared`. **So there is no fluent chaining**
   (`stmt.text(1, "Ada").integer(2, 36)` cannot be written). Binding must either be separate
   statements or - better - take an array of values in one call. This single fact decides the
   shape of the whole parameter API.
2. **Interfaces cannot be generic.** `parse_interface` takes no type parameters, so
   `interface Mapper<T>` is out. A generic *struct* with a function-valued member or parameter
   does the same job (`Rows<T>.all(read: func(Row): T)`), and that is what Part 3 uses.
3. **`for ... in` walks arrays, strings and maps only** (`S052` otherwise): a result set is not
   iterable. A cursor therefore offers `while rows.has_next()` *and* an `all()` that materialises
   `Row[]` for the `for` loop - the same pair the concurrency `Channel` offers.
4. **A native call blocks every task.** Documented in the concurrency reference: while C++ runs,
   the scheduler cannot switch. A long query freezes timers and the drawing loop. It is the same
   gap the concurrency report lists for sockets and `os.run`, and the same fix would serve all
   three (phase 2 there); until then it is a documented limit, and `:memory:` or a small local
   file hides it in teaching use.

Two more, smaller: a native function cannot *return* a function value (parameters only), and a
C++ callback into Thunder runs nested on the C++ stack, so a task may not wait inside one
(`R038`) - which rules out `sqlite3_exec`-style row callbacks and argues for the
`prepare`/`step` cursor, which is the better API anyway.

---

## Part 3 - The proposed library

### 3.1 Three layers

```
  a program            import "sqlite"      (and "database" for Row, Value, Table)
      |
  driver module        lib/thunder/<lang>/sqlite.th    struct Sqlite: database.Driver
      |                 import native "thunder_sqlite"
  the objects          lib/thunder/<lang>/database.th  Value Row Rows Statement
      |                                                Connection Transaction Table<T>
  the C++ driver       lib/native/sqlite/thunder_sqlite.cpp   (handles, like the sockets module)
```

`database.th` knows no database: it holds the objects and `interface Driver`. One file per
driver implements that interface by forwarding to its own `native func`s. A second driver is a
second pair of files and **no change** to anything above it.

### 3.2 The driver contract

Everything a driver must do is already scalar-shaped, which is why it fits the native boundary
without a single conversion:

```thunder
interface Driver
    func name(): string
    func open(target: string): int                  # a connection handle, or -1
    func close(connection: int)
    func prepare(connection: int, sql: string): int # a statement handle, or -1
    func bind_text(statement: int, position: int, value: string)
    func bind_integer(statement: int, position: int, value: int)
    func bind_real(statement: int, position: int, value: float)
    func bind_blob(statement: int, position: int, value: byte[])
    func bind_null(statement: int, position: int)
    func step(statement: int): bool                 # true while a row is there
    func column_count(statement: int): int
    func column_name(statement: int, position: int): string
    func column_kind(statement: int, position: int): int
    func column_text(statement: int, position: int): string
    func column_integer(statement: int, position: int): int
    func column_real(statement: int, position: int): float
    func column_blob(statement: int, position: int): byte[]
    func finish(statement: int): int                # rows changed; frees the statement
    func last_id(connection: int): int
    func error(): string
```

Nineteen methods, each a one-line forward in the driver's `.th`, each a few lines of C++. They
map one-to-one onto `sqlite3_prepare_v2` / `_bind_*` / `_step` / `_column_*` / `_finalize`, onto
ODBC's `SQLPrepare` / `SQLBindParameter` / `SQLFetch` / `SQLGetData`, and onto libpq (which
hands back a whole result, so `step` walks it).

### 3.3 What a program writes

```thunder
import "database"
import "sqlite"

with db = sqlite.open("school.db")
    if db == nothing
        fail("cannot open the database: " + database.error())

    db.run("create table if not exists people (name text, age integer)")

    # Values are bound, never pasted into the text: this is where injection is taught.
    with tx = db.transaction()
        db.run_with("insert into people (name, age) values (?, ?)",
                    [value.text("Ada"), value.integer(36)])
        db.run_with("insert into people (name, age) values (?, ?)",
                    [value.text("Grace"), value.integer(45)])
        tx.commit()                      # without this the block rolls back

    for row in db.query_with("select name, age from people where age > ?",
                             [value.integer(30)]).all()
        println(row.text("name"), " is ", row.integer("age"))

    # A large result, one row at a time, like a channel.
    with rows = db.query("select name from people order by name")
        while rows.has_next()
            println(rows.next().text("name"))
```

The objects:

| Object | Members |
| --- | --- |
| `Connection` | `run(sql): int`, `run_with(sql, values): int`, `query(sql): Rows`, `query_with(sql, values): Rows`, `prepare(sql): Statement`, `transaction(): Transaction`, `last_id(): int`, `is_open(): bool`, `close()` |
| `Statement` | `bind(values: Value[])`, `run(): int`, `rows(): Rows`, `close()` - a prepared statement run many times |
| `Rows` | `has_next(): bool`, `next(): Row`, `all(): Row[]`, `columns(): string[]`, `close()` |
| `Row` | `text(name)`, `integer(name)`, `real(name)`, `blob(name)`, `is_null(name)`, the same five `*_at(position)`, `value(name): Value` |
| `Value` + `enum Kind` | `value.text(s)`, `value.integer(n)`, `value.real(x)`, `value.blob(b)`, `value.null()`; `v.kind`, `v.describe(): string` |
| `Transaction` | `commit()`, `rollback()`, `close()` (rolls back unless committed) |

**Why `run_with(sql, values)` and not binding call by call:** there is no `self`, so chaining is
impossible (Part 2.1); and one call with an array reads better than four statements. A prepared
`Statement` still exists for the "same query, many rows" case.

**Errors.** The sockets rule, which students already know: what the program can handle comes back
as `nothing` or `false` with `database.error()` saying why (file missing, constraint violated,
SQL that will not parse); what the program got wrong stops it with `fail` (a closed connection,
a column that is not in the row).

**`null`.** A native function returning a string cannot also say "this was null", so `Row` asks
`is_null(name)` first; `text()` of a null column answers `""` and `integer()` answers `0`, with
`value(name)` giving the tagged truth. This is the one place the design pays for having no
option type, and it is worth a paragraph in the documentation.

---

## Part 4 - The mapping (the "OR" part)

### 4.1 What is possible today: a mapper the program writes once

```thunder
struct Person
    name: string
    age: int

func person_of(r: Row): Person
    return Person(name: r.text("name"), age: r.integer("age"))

func person_values(p: Person): Value[]
    return [value.text(p.name), value.integer(p.age)]

var people: Table<Person> = Table(db, "people", ["name", "age"], person_of, person_values)

people.create_if_missing("name text, age integer")
people.insert(Person(name: "Ada", age: 36))
for person in people.where("age > ?", [value.integer(30)])
    println(person.name)
println(len(people.all()))
```

`Table<T>` is an ordinary generic struct holding the connection, the table's name, its column
names and the two function values. It writes the SQL (`select <columns> from <table>`,
`insert into <table> (<columns>) values (?, ?)`) and uses `Rows<T>.all(read)` to build the
objects. About 80 lines of Thunder, all of it readable - which is the SDK's whole habit: a
student can open `database.th` and see what an ORM actually does.

**This is the recommendation.** It is honest (no magic), it teaches the mapping instead of
hiding it, and it costs no language change.

### 4.2 What the declarative kind would need

A `@table("people")` / `@column` ORM that reads a struct and writes its own SQL needs three
things Thunder does not have:

1. **User-defined annotations.** `known_annotations` is a fixed array of two (`test`, `mock`);
   anything else is `P021`. Opening it up means a way for a library to declare which annotations
   it understands, and a way for a program to read them back.
2. **Reflection over fields.** `inspect()` proves the virtual machine has every field's name and
   type at run time (the `.thc` struct table carries them). A built-in
   `fields(value): (string, string)[]` would be a small addition.
3. **Setting a field by name.** `CALL_METHOD` already looks a *method* up by name at run time,
   so the machinery exists; a `set_field(value, name, v)` would need the tagged `Value` of this
   library (or a built-in per type) to say what it is setting.

That is a project in its own right, and it would make the library less readable, not more. My
advice is to leave it and revisit only if a course actually asks for it.

### 4.3 Two things that would make 4.1 nicer, cheaply

* **The deduced single-expression lambda** already on the table (`func(r) -> Person(...)`) turns
  the mapper into one line at the call site.
* **A generated mapper** is a third way: `thunderc --mapper Person` could *print* the two
  functions for a struct, which the programmer pastes in. No language change, no magic at run
  time, and the student still sees the code. Worth considering after the library exists.

---

## Part 5 - Which database first

**SQLite, vendored.** It is one public-domain C file (the "amalgamation"), needs no server, no
account and no installation, works the same on Linux, Windows and macOS, and is what a course
would use anyway. Vendoring it under `lib/native/sqlite/` means `import "sqlite"` works the
moment the toolchain is built, which is the point of an SDK module; `--package` already copies
native libraries next to the executable, so a packaged program keeps working. The cost is a
large generated C file in the tree and a slower first build of that one target.

The alternative - `find_package(SQLite3)` and skip it when absent, as the SDL display plug-in
does - keeps the tree small but makes the module sometimes-there, which is wrong for something
the samples and the documentation lean on.

Afterwards, in order of usefulness: **PostgreSQL** through libpq (optional, built when found,
exactly the SDL pattern), then **ODBC** on Windows, which reaches SQL Server and Access and is
part of the platform. Both are new `.th` + `.cpp` pairs and no change above them - which is the
test of whether the abstraction in 3.2 was right.

---

## Part 6 - Phases

1. **The driver and the objects** (one medium iteration): `thunder_sqlite.cpp` with the handle
   table; `database.th` and `sqlite.th` in both languages; `with`, `nothing`/`error()` and
   `fail` wired as above; `docs/database.md` + `docs/ps/database.md`; a sample; `@test` suites
   against a temporary file and `:memory:`; the API site.
2. **The mapping**: `Table<T>`, `Rows<T>.all`, the two-function mapper, a second sample that
   stores and reads a struct.
3. **A second driver** (PostgreSQL or ODBC), which is where the `Driver` interface earns its
   keep - and the moment to fix anything that turns out to be SQLite-shaped.
4. **Optional**: the database on a helper thread so a query does not stop the other tasks
   (shared with the concurrency report's phase 2); `thunderc --mapper`; the declarative ORM.

---

## Part 7 - Decisions for you

1. **Vendor SQLite** (works everywhere, big file in the tree) or **find it** (small tree,
   sometimes missing)? My recommendation is to vendor.
2. **The mapping**: hand-written mappers with `Table<T>` (recommended), or hold the mapping back
   until the language can do it declaratively?
3. **`run_with(sql, values)` as the main way to pass values**, with `Statement` for repeated
   queries - or always go through `prepare`?
4. **Rows**: `all()` materialising `Row[]` for the `for` loop *and* `has_next()`/`next()` for
   large results (recommended), or only one of the two?
5. **The name**: `database` for the objects and `sqlite` for the driver - or `db` and
   `db.sqlite`? (The SDK's files are flat today; a folder would be a first.)

## Pashto names (drafts for a native speaker, like all the vocabulary)

`database` ډېټابېس, `Connection` اړیکه, `Statement` غوښتنه, `Rows` کرښې, `Row` کرښه,
`Value` ارزښت, `Kind` ډول, `Transaction` راکړه‌ورکړه, `Table` جدول, `Driver` چلوونکی;
`open` پرانیزه, `run` وچلوه, `query` پوښتنه, `prepare` چمتو کړه, `commit` ومنه,
`rollback` بېرته یې کړه, `has_next` بل شته, `next` بل, `all` ټول, `columns` کالمونه,
`is_null` تش دی, `last_id` وروستۍ شمېره, `error` تېروتنه.
