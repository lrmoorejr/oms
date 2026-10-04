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
- Header-only: copy `Oms.hpp` (and optionally `OmsString.hpp` and `OmsText.hpp`) into your project
- Optionally depends on [Ensure.hpp](https://github.com/lrmoorejr/ensure) for `ensure()` /
  `throw_if()` / `caution()`. Falls back to `assert()` and built-in equivalents when
  Ensure.hpp is not present.

## Files

| File | Purpose |
|------|---------|
| `Oms.hpp` | Core library: all types, serialization |
| `OmsString.hpp` | Human-readable dump formatting (`toString()`) |
| `OmsText.hpp` | Lossless text form and its parser (`toText()` / `fromText()`, `writeText()` / `readText()`) |
| `OmsDump.cpp` | `omsdump` CLI — prints any `.oms` file to stdout, and converts between binary and text |

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

### Text format (OmsText.hpp)

`toString()` is for looking at. The text written by `OmsText.hpp` keeps everything, so it can
be read back into the same data: the same members in the same order, each with the same type
and the same value, bit for bit. It is also meant to be written by hand.

```
section "model" {
   splitPoint: 240,
   components: [
      {
         class: "Resynthesizer",
         visible: true,
         redAmplification: float4(39),
         "real time": 1723312000.25,
         point: int16[180, 94],
         signal: blob(0000803f 0000003f)
      }
   ]
}
```

| OMS type | Text | Notes |
|----------|------|-------|
| `structure` | `{ key: value, ... }` | Members keep their order |
| `array` | `[ { ... }, { ... } ]` | A bare `[` always holds structures |
| `string` | `"text"` | Escapes: `\"` `\\` `\n` `\r` `\t` `\xHH` |
| `boolean` | `true`, `false` | |
| `int32` | `42`, `-7` | What a bare integer means |
| `float8` | `0.5`, `2e-3`, `nan`, `inf`, `-inf` | What a bare number with a `.` or an exponent means |
| other scalars | `uint8(200)`, `int64(-5)`, `float4(0.5)` | The type name, then the value |
| vectors | `int16[180, 94]`, `float4[]` | The element type name, then the elements |
| `blob` | `blob(0000803f 0000003f)` | Hexadecimal, two digits per byte |

- The type names are `uint8` to `uint64`, `int8` to `int64`, `float4` and `float8`. Any scalar
  may be given its type, as in `int32(5)`. A number that does not fit its type, such as
  `uint8(300)`, is an error.
- A float is written in the shortest form that reads back to the same bits. The one thing
  text does not carry is the payload of a NaN, which is written as `nan` or `-nan`.
- A key is written bare when it is made of letters, digits, `_` and `.` and does not start
  with a digit or a `.`. Any other key is a quoted string. A repeated key is an error.
- Members and elements are separated by commas, and a trailing comma is allowed. Line breaks
  and indentation carry no meaning. `//` starts a comment that runs to the end of the line.

```cpp
#include "OmsText.hpp"

// One Structure <-> "{ ... }"
std::string text = oms::toText(structure);
oms::Structure settings = oms::fromText("{ gain: 0.5, taps: 3 }");
float gain = settings.getOr("gain", 1.0f);   // a bare 0.5 is a float8, and converts as usual

// One Section <-> "section "name" { ... }", one after another for a whole file
oms::writeText(textOut, section);            // unlike operator<<, leaves the section as it was
oms::TextPosition position;
while(oms::readText(textIn, section, position))
    binaryOut << section;
```

| Function | Description |
|----------|-------------|
| `toText(structure, options)` | Returns the text of a `Structure` |
| `fromText(text)` | Returns the `Structure` that a text of the form `{ ... }` describes |
| `writeText(ostream, section, options)` | Writes one `Section` as `section "name" { ... }` |
| `readText(istream, section, position)` | Reads the next `Section`; returns `false` when none is left |

Text that cannot be read throws `oms::ParseError`, a `std::runtime_error` with `line` and
`column` members whose `what()` reads `line:column: message`. Passing the same `TextPosition`
to every `readText()` call on a stream keeps those counted from the start of the stream.

`options` is an `oms::TextOptions`, and by default everything is written. Setting
`maxVectorElements`, `maxBlobBytes` or `maxArrayElements` gives a summary, in which a larger
vector, blob or array is replaced by its size and a hash of its contents:

```
samples: float4[4096 elided 9f3c2a1b],
image: blob(131072 bytes elided 5d02e7a4),
records: [2008331 elided 418b0da2]
```

The hash changes when the contents do, so a comparison of two summaries shows that the data
differs without showing the data. An array's hash covers everything the array holds, whatever
the other limits are. A summary cannot be read back.

### omsdump utility

```sh
omsdump data.oms                        # dump all sections
omsdump data.oms config                 # dump only the section named "config"
omsdump --list data.oms                 # list section names and byte counts
omsdump --text data.oms                 # lossless text, all sections
omsdump --text data.oms config          # lossless text, one section
omsdump --text --summary data.oms       # text with the bulk elided (see below)
omsdump --text --summary=256 data.oms   # the same, with a higher limit
omsdump --binary data.txt data.oms      # text back to binary; a text file of - reads stdin
```

`--summary=N` elides vectors of more than N elements, blobs of more than 4N bytes, and arrays of
more than 16N structures. N is 16 unless given, so an array is kept up to 256 structures.

`--text` followed by `--binary` reproduces a file byte for byte, provided its sections record
their size (see `sectionSize()`). A section that was written to a non-seekable stream records
none, and gains one. On text that cannot be read, `--binary` prints `file:line:column: message`,
writes no output file, and exits non-zero.

To have git show changes to binary OMS files as text:

```sh
echo '*.oms diff=oms' >> .gitattributes
git config diff.oms.textconv 'omsdump --text --summary'
```

## Wire format

All multi-byte integers are written in **little-endian** byte order. Each `Section` begins
with two 64-bit GUIDs followed by the total section byte count (enabling O(1) forward-skip),
two reserved `size_t` fields, and the section name. Schema tolerance is built in:
unrecognised member types are skipped on read, so adding new fields to a writer never
breaks older readers.

## License

Apache 2.0 — see [LICENSE](https://github.com/lrmoorejr/oms/blob/main/LICENSE).
