# Plan: Migrate `json_handler` from Boost.PropertyTree to Boost.JSON

## 1. Goal & hard constraint

Replace `boost::property_tree` JSON generation in `nano/node/json_handler.cpp`
(5,463 lines, ~505 ptree call sites) with `boost::json`, which serializes
dramatically faster than `property_tree::write_json`.

**Non-negotiable backward-compat rule:** `property_tree::write_json` emits *every*
scalar leaf as a **quoted string**, regardless of its C++ type:

| C++ call | property_tree output |
|---|---|
| `put("count", 5)` | `"count":"5"` |
| `put("confirmed", true)` | `"confirmed":"true"` |
| `put("multiplier", 1.0)` | `"multiplier":"1"` (uses ptree's float policy) |

The Boost.JSON replacement must reproduce this byte-for-byte. External consumers
(exchanges, wallets, light clients) and our own `rpc_test` harness parse responses
back with `property_tree::read_json` and call `get<bool>`, `get<double>`,
`get<uint128_t>` on these *quoted* values. Emitting a native JSON number/boolean
would be an API break.

## 2. Why this is non-trivial

- The quoting quirk means we cannot use `boost::json`'s native scalar types. Every
  leaf must be stored as a JSON *string*.
- The exact string for non-string types (`bool` → `"true"/"false"`, `double`
  precision, integer formatting) is produced today by property_tree's
  `stream_translator`. We must reuse *the same* translator to guarantee identical
  bytes — not hand-roll `std::to_string`.
- `property_tree` arrays are encoded as children with empty keys
  (`entry.put(""); arr.push_back(std::make_pair("", entry))`, 28 sites). Boost.JSON
  has real arrays; the wrapper must hide this difference.
- Several values are *foreign* ptrees produced by other subsystems and embedded via
  `add_child`/`put_child`: `block->serialize_json(ptree)`, `node.bootstrap.info()`,
  the database txn tracker, telemetry metrics, and `container_info` (`construct_json`).
  These need a ptree → Boost.JSON bridge.
- `accounts_receivable` sorts a ptree in place (`peers_l.sort(...)`, 2 sites).

## 3. Design — adapter wrapper, not a full rewrite

Two approaches were considered:

- **(A) Direct rewrite** to `boost::json::object`/`array` at every call site —
  ~505 edits, each must manually stringify, high risk of subtle format drift.
- **(B) Thin adapter** that preserves the existing `put` / `add_child` / array API
  but is backed by `boost::json` and force-stringifies scalars in one place.

**Recommendation: (B).** It keeps the 505 call sites almost untouched, centralizes
the compat rule in one type, and makes the diff reviewable.

### 3.1 New type `nano::json::object_writer` (working name)

In a new header `nano/lib/json_writer.hpp` (+ `.cpp`):

```cpp
namespace nano::json
{
class object_writer
{
public:
    // Scalar: convert T -> std::string via property_tree's translator,
    // then store as a JSON string. Guarantees identical bytes to write_json.
    template <typename T>
    void put (std::string const & key, T const & value);

    // Nested object / array
    void add_child (std::string const & key, object_writer const & child);
    void put_child (std::string const & key, object_writer const & child); // alias
    array_writer & put_array (std::string const & key); // helper for new code

    bool empty () const;
    boost::json::object const & value () const;
    std::string serialize () const; // boost::json::serialize

private:
    boost::json::object obj;
};

class array_writer { /* push(scalar) and push(object_writer) */ };
}
```

### 3.2 The compat core (most important detail)

`put` must stringify with the **same translator property_tree uses**, so the output
is identical:

```cpp
template <typename T>
void object_writer::put (std::string const & key, T const & value)
{
    if constexpr (std::is_same_v<T, std::string> || std::is_convertible_v<T, std::string>)
    {
        obj[key] = boost::json::string (std::string (value));
    }
    else
    {
        // Reuse property_tree's stream_translator: bool -> "true"/"false",
        // doubles/ints formatted exactly as before.
        boost::property_tree::stream_translator<char, std::char_traits<char>,
            std::allocator<char>, T> tr;
        obj[key] = boost::json::string (tr.put_value (value).value ());
    }
}
```

(Exact translator spelling to be finalized during implementation; the principle is:
**do not invent formatting — delegate to the translator that produced today's
output.**) Add a focused unit test asserting `put(bool)`, `put(double)`,
`put(uint64_t)`, `put(uint128_t)` match `write_json` of the equivalent ptree.

### 3.3 ptree → Boost.JSON bridge

For embedded foreign ptrees (blocks, bootstrap info, telemetry, container_info):

```cpp
boost::json::value nano::json::from_ptree (boost::property_tree::ptree const &);
```

Recursive: distinguishes object vs array (ptree array = all-empty-keys), copies leaf
`data()` verbatim as a JSON string (preserving the quoting contract). Used at the
~12 `add_child`/`put_child` sites that consume non-`json_handler` ptrees.

### 3.4 Scope boundary: keep the request side on property_tree

The performance complaint is about **output** (`write_json`). The **request** is
parsed once per call and feeds `nano::deserialize_block_json(ptree)` and 157
`request.get<...>` reads. Converting it adds large surface area and risk for no
stated benefit.

**Decision: leave `request` as `boost::property_tree::ptree`.** Only `response_l`,
`response_error`, and the async response builders change. This roughly halves the
blast radius. (A follow-up can migrate request parsing if profiling justifies it.)

## 4. Build changes

`CMakeLists.txt` (line ~441, `BOOST_INCLUDE_LIBRARIES`): add `json`. Boost is the
in-tree submodule (`add_subdirectory(submodules/boost)`), so adding the module name
is sufficient. Then link `Boost::json` in `nano/node/CMakeLists.txt` (alongside
`Boost::beast`, etc.). Confirm `Boost::json` is also visible to any other target
that ends up including the new header (likely just `node`).

## 5. Migration mechanics

`json_handler.hpp` changes:
- `boost::property_tree::ptree response_l;` → `nano::json::object_writer response_l;`
- Keep `boost::property_tree::ptree request;` unchanged.
- `#include <nano/lib/json_writer.hpp>`.

`json_handler.cpp` — mostly mechanical, the wrapper preserves call shapes:

| Old (property_tree) | New (object_writer) |
|---|---|
| `response_l.put("k", v)` | unchanged |
| `response_l.add_child("k", child)` | unchanged (child is `object_writer`) |
| `boost::property_tree::ptree entry;` (local builder) | `nano::json::object_writer entry;` |
| `entry.put(""); arr.push_back(make_pair("", entry));` | `arr.push (entry);` (array_writer) |
| `arr.put("", scalar); push_back(make_pair("",e))` | `arr.push (scalar);` |
| `peers_l.sort(...)` | sort the underlying `boost::json::array` by string→`uint128_t` compare |
| `block->serialize_json(block_node_l); add_child("contents", block_node_l)` | build ptree as today, `add_child("contents", from_ptree(block_node_l))` |
| `add_child("bootstrap", node.bootstrap.info())` | `add_child("bootstrap", from_ptree(node.bootstrap.info()))` |
| `write_json(ostream, response_l); response(ostream.str())` | `response(response_l.serialize())` |

`response_errors()` (line 169) and the async callbacks that build their own ptree +
`write_json` (e.g. `account_representative_set` line ~875, `block_create`
`block_response_put_l` line ~1636) convert to `object_writer` + `serialize()`.

### Inventory to drive the work
- `put` / scalar sites: bulk, low-risk (wrapper handles them).
- `add_child` / `put_child`: 65 sites — audit each for foreign-ptree vs local.
- `push_back(make_pair("", ...))` array idiom: 28 sites.
- `.sort(...)`: 2 sites (`accounts_receivable`).
- Standalone `write_json`/`read_json` in `json_handler.cpp`: the response writes
  (convert) vs `block_impl` request reads (leave — still ptree).

## 6. Edge cases / gotchas

1. **`empty()` semantics** — `response_errors()` checks `response_l.empty()` to
   decide `empty_response`. Wrapper must report empty when no keys were added.
2. **Error response** — currently a fresh ptree `{ "error": msg }`; convert to a
   tiny `object_writer`. Keep `json_error_response` (in `nano/lib/`) consistent;
   check whether it also uses property_tree and align it.
3. **Insertion order** — `boost::json::object` preserves insertion order, matching
   property_tree, so field ordering is unchanged (important for any client doing
   loose string matching).
4. **Number-keyed objects** — handlers use account/hash strings as keys
   (`peers_l.put(hash, amount)`); fine, they're strings.
5. **Empty array vs object** — some responses add an empty child meaning "{}"
   (e.g. `add_child("blocks", pending)` when empty). Preserve whether each empty
   container serializes as `{}` or `[]` to match today's output exactly — verify
   per-site in tests.
6. **Duplicate keys** — property_tree allows dup keys; `boost::json::object` does
   not. Audit array-style sites that used dup empty keys (the `push_back` idiom) —
   these become real arrays, which is correct, but confirm none relied on dup
   *named* keys.

## 7. Testing strategy

- **Unit test** for `object_writer`/`from_ptree`: for a representative value of each
  type, assert `object_writer.serialize()` == `write_json` of the equivalent ptree
  (the byte-for-byte compat gate).
- **Full `rpc_test` suite** (`nano/rpc_test/`, ~8k lines) is the real safety net:
  it parses responses with property_tree and asserts on `get<bool>`,
  `get<double>`, `get<uint128_t>`, child structure. Must pass unchanged.
- **Golden/diff check during development:** add a temporary mode (or local script)
  that runs both serializers on the same `response_l` and asserts equality, to catch
  format drift across many handlers cheaply before deleting the ptree path.
- Spot-check a few high-value endpoints by hand: `account_info`, `block_info`,
  `blocks_info` (json_block true/false), `account_history`, `ledger`, `telemetry`,
  `stats`, `confirmation_info` — these exercise nesting, arrays, foreign ptrees,
  and sorting.

## 8. Phasing (keeps each PR reviewable and revertible)

1. **PR1 — infrastructure (no behavior change):** add `Boost::json` to the build;
   add `nano/lib/json_writer.{hpp,cpp}` with `object_writer`, `array_writer`,
   `from_ptree`; add the compat unit test. Nothing in `json_handler` uses it yet.
2. **PR2 — switch `response_l` type + plumbing:** change `response_l`,
   `response_errors`, error response, and the async response builders. Migrate the
   simple `put`-only handlers. Keep `from_ptree` for all `add_child` sites.
3. **PR3 — migrate array idioms and `.sort`** to `array_writer`.
4. **PR4 — optional cleanup:** migrate local-ptree builders that feed `add_child`
   to native `object_writer` (drop `from_ptree` where the source is ours), reducing
   conversions. Foreign ptrees (`bootstrap.info`, telemetry, container_info) keep
   `from_ptree`.
5. Remove now-unused `property_tree` includes from `json_handler.cpp` where the
   response path no longer needs them (request path still includes ptree).

## 9. Risks & mitigations

- **Format drift (the whole point):** mitigated by reusing property_tree's
  translator in `put`, the byte-equality unit test, and the temporary dual-serialize
  diff check.
- **`Boost::json` build wiring** on all platforms (Win/Mac/Linux CI): isolated in
  PR1 so a build break is caught before any handler churn.
- **Empty `{}` vs `[]` serialization** mismatches: caught by rpc_test + golden diff;
  enumerate empty-container sites in PR2.
- **Large mechanical diff:** phased PRs + the wrapper preserving call shapes keep
  review tractable.

## 10. Out of scope (call out explicitly)

- Request parsing stays on property_tree (see §3.4).
- `block->serialize_json(ptree)` and other subsystems' ptree producers are *not*
  rewritten; bridged via `from_ptree`. A native `serialize_json(boost::json)`
  overload on blocks is a possible later optimization, not required here.
- IPC/flatbuffers handler is untouched.
