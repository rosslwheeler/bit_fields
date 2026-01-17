# Coding Standards

- **Language level:** C++20, header-only interfaces where practical; prefer `std::string_view` and `std::span` for zero-copy views.
- **Namespaces:** Everything lives under `nic` (and sub-namespaces by subsystem: `dma`, `queues`, `trace`).
- **Ownership & errors:** Use RAII and strong types; avoid raw `new/delete`; prefer `std::optional`/`expected`-style return types for recoverable errors (wrap your own until `<expected>` is available).
- **Threading model:** Keep core modeling code single-threaded and deterministic; isolate any parallelism behind clear interfaces.
- **Tracing/logging:** Use the Tracy-backed helpers in `nic/trace.h`; keep instrumentation at subsystem boundaries.
- **Testing:** Every module gets unit tests; scenario tests go under `tests/` and run via `ctest`. Favor deterministic inputs and explicit drop/error reasons.
- **Style:** Consistent brace placement, `snake_case` for functions/variables, `PascalCase` for types. Keep headers self-contained and minimal. namespace using instead of braces in cpp files. MUST use braces for all if statements. Avoid using the ? operation - ternary. In for loops - make sure the index has context related to the loop.
- **Sizes:** functions less than 100 lines. Don't put functions greater than 10 lines in a class header.
- **Tracing:** make sure all functions have this macro ->   NIC_TRACE_SCOPED(__func__);
# C++ Coding Preferences

**General**
- Target **C++20** where available. Prefer standard library features over third-party deps.
- **No raw `new`/`delete`**. Use RAII and smart pointers (`std::unique_ptr` first; `std::shared_ptr` only when shared ownership is required).
- **Do not** write `using namespace std;`. Qualify names or use selective `using`.
- Favor **`const` correctness**, **`constexpr`**, **`noexcept`** where appropriate.
- Prefer **`enum class`** over unscoped enums.
- Prefer **`std::string_view`** for non-owning text and **`std::span<T>`** for non-owning buffers.
- Prefer **range-based for** and **`std::ranges`** algorithms when it improves clarity.
- Prefer **std-optional** algorithms when it improves clarity.
- Prefer **noexcept** on get member functions.
- Prefer **[[nodiscard]]** on get member functions or functions that return a value.
- Prefer **explicit** from single variable constructors.
- Prefer **** from single variable constructors.


**Error handling**
- Use **exceptions** for non-recoverable errors and API contracts. 
- Use **`std::optional<T>`** for "missing value" cases.
- For recoverable errors with payloads, prefer **`std::expected<T, E>`** (if toolchain supports it); otherwise use a struct return type with status + value.

**API & Style**
- **Naming**: `PascalCase` for types; `snake_case` for functions/variables; `kCamelCase` for constants; macros in `ALL_CAPS`.
- **Headers**: keep light; use `#pragma once`. Separate interface (.hpp) from implementation (.cpp).
