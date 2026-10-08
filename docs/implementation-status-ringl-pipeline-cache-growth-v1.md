# RinGL pipeline-cache growth v1

The GL graphics-pipeline cache starts with 32 entries and grows geometrically
to a hard limit of 512. Each candidate table is reserved against the per-context
512 MiB CPU budget before allocation. Growth copies a full ring in oldest-to-
newest eviction order, publishes the replacement only after allocation
succeeds, and releases the old reservation after freeing its table.

If the budget or allocator rejects a larger table, the old cache remains valid
and the existing bounded eviction path admits the newly created pipeline. Cache
teardown releases the active table's exact reservation and all cached pipeline
artifacts.

The CMake `RinGL` target builds with this change. No tests were added or run.
Backend-owned GPU memory accounting and hardware/QEMU resource accounting stay
open in the broader allocation audit.
