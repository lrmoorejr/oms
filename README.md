# oms

[API docs](https://lrmoorejr.github.io/oms/)

**One More Serializer** — a single-header C++20 binary serialization library.

OMS stores typed data in a hierarchical key-value model and serializes it to a compact
little-endian binary format. The `Section` wrapper adds GUID-tagged framing that supports
random access by name and streaming of data sets that exceed available memory — write
millions of records sequentially without loading them all at once.

```cpp
#include "Oms.hpp"
#include "OmsString.hpp"

// Write
oms::Section section;
section.name = "config";
section.add("version", std::uint32_t{1});
section.add("label", std::string{"hello"});
section.add("weights", std::vector<float>{1.0f, 2.0f, 3.0f});
std::ofstream out("data.oms", std::ios::binary);
out << section;   // writes and clears the section automatically

// Read
std::ifstream in("data.oms", std::ios::binary);
oms::Section s;
in >> s;
std::uint32_t ver = s.getOr("version", std::uint32_t{0});
std::cout << oms::toString(s) << '\n';
```

## Requirements

- C++20 or later
- Header-only: copy `Oms.hpp` (and optionally `OmsString.hpp`) into your project
- Optionally depends on [Ensure.hpp](https://github.com/lrmoorejr/ensure) for `ensure()` /
  `throw_if()` / `caution()`. Falls back to `assert()` and built-in equivalents when
  Ensure.hpp is not present.

## Files

| File | Purpose |
|------|---------|
| `Oms.hpp` | Core library: all types, serialization |
| `OmsString.hpp` | Human-readable dump formatting (`toString()`) |
| `OmsDump.cpp` | `omsdump` CLI — prints any `.oms` file to stdout |

## API

### oms::Section — the top-level streaming unit

```cpp
oms::Section section;
section.name = "my-section";        // required before writing
out << section;                     // writes header + body, then clears
in  >> section;                     // reads next section from stream
```

`Section` inherits all of `Structure`'s `add*` / `get*` methods.

| Method | Description |
|--------|-------------|
| `name` | Section name (read/written verbatim) |
| `sectionSize()` | Total byte count from GUID through end of body (0 on non-seekable streams) |
| `clear()` | Clears all members and resets `name` |
| `Section::findNext(in, name)` | Scans forward and returns the named section, or `std::nullopt` |

**Streaming write pattern** — reuse one `Section` object; `operator<<` clears it after each flush:

```cpp
for(auto& record : records) {
    section.name = "record";
    section.add("id", record.id);
    out << section;
}
```

**Random-access read pattern**:

```cpp
std::ifstream in("data.oms", std::ios::binary);
if(auto result = oms::Section::findNext(in, "config"))
    process(*result);
```

### oms::Structure — key-value container

Members are stored in insertion order. All `add*` methods overwrite an existing member;
`getOrAdd*` methods leave an existing member untouched.

#### Read methods

| Method | Description |
|--------|-------------|
| `operator[](key)` | Returns member by name; throws `std::out_of_range` if absent |
| `get(key)` | Synonym for `operator[]` |
| `get<T>(key, vector)` | Copies a blob member into `std::vector<T>` |
| `getOr<T>(key, default)` | Returns stored value or default; never inserts |
| `contains(key)` | Returns `true` if the member exists |
| `empty()` / `size()` | Member count |
| `getEntries()` | Member names in insertion order |

#### Write methods

| Method | Description |
|--------|-------------|
| `add(key, value)` | Scalar, `std::string`, `const char*`, or raw blob |
| `add(key, vector)` | `std::vector<T>` stored as a blob |
| `addVariant(key, variant)` | Deep-copies any `Variant` |
| `addStructure(key)` | Creates a nested `Structure`, returns reference |
| `addArray(key)` | Creates a nested `Array`, returns reference |
| `addVector<T>(key, ...)` | Creates a typed `Vector<T>`, returns reference |
| `getOrAdd(key, value)` | Insert-if-absent for scalars / blobs |
| `getOrAddStructure(key)` | Insert-if-absent nested Structure |
| `getOrAddArray(key)` | Insert-if-absent nested Array |
| `getOrAddVector<T>(key)` | Insert-if-absent typed Vector |
| `clear()` | Removes all members |

#### index member

`const std::optional<std::size_t> index` — set automatically when a `Structure` is an
element of an `Array`; `std::nullopt` otherwise.

### oms::Array — ordered sequence of Structures

```cpp
Array& arr = section.addArray("results");
Structure& row = arr.addStructure();
row.add("score", 0.95);
```

| Method | Description |
|--------|-------------|
| `addStructure()` | Appends a new Structure; sets its `index` |
| `operator[](i)` | Indexed element access |
| `size()` / `empty()` | Element count |

### oms::Variant — base type for all values

All members of `Structure` and `Array` are `Variant` references. Cast with the implicit
conversion operators:

```cpp
const Variant& v = section["label"];
std::string s = v;            // operator std::string()
std::uint32_t n = section["version"];
```

> **Note:** There is no `operator const char*`. Comparing with a string literal requires
> `std::string_literals`:
> ```cpp
> using namespace std::string_literals;
> bool match = (section["label"] == "hello"s);
> ```

### Supported types

| OMS type | C++ type |
|----------|----------|
| `string` | `std::string` |
| `boolean` | `bool` |
| `int8` / `uint8` | `int8_t` / `uint8_t` |
| `int16` / `uint16` | `int16_t` / `uint16_t` |
| `int32` / `uint32` | `int32_t` / `uint32_t` |
| `int64` / `uint64` | `int64_t` / `uint64_t` |
| `float4` / `float8` | `float` / `double` |
| `blob` | `void*` + size |
| `int8v` … `float8v` | `Vector<T>` of the corresponding scalar type |
| `structure` | `Structure` |
| `array` | `Array` |

### oms::toString() (OmsString.hpp)

```cpp
#include "OmsString.hpp"
std::cout << oms::toString(section);
```

Produces an indented, JSON-like string. Keys are sorted alphabetically. Nested structures
and arrays are expanded recursively. Blobs appear as `(blob)`.

### omsdump utility

```sh
omsdump data.oms                # dump all sections
omsdump data.oms config         # dump only the section named "config"
omsdump --list data.oms         # list section names and byte counts
```

## Wire format

All multi-byte integers are written in **little-endian** byte order. Each `Section` begins
with two 64-bit GUIDs followed by the total section byte count (enabling O(1) forward-skip),
two reserved `size_t` fields, and the section name. Schema tolerance is built in:
unrecognised member types are skipped on read, so adding new fields to a writer never
breaks older readers.

## License

Apache 2.0 — see [LICENSE](https://github.com/lrmoorejr/oms/blob/main/LICENSE).
