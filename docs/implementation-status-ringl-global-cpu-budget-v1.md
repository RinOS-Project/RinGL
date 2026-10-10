# RinGL aggregate CPU allocation budget

RinGL contexts continue to enforce the existing 512 MiB per-context CPU shadow
budget. The context allocator now also applies a 1 GiB aggregate ceiling across
all contexts in one loaded RinGL library instance. The aggregate includes each
allocated context record, every active reservation made through the context
shadow accounting helper, and retained staging-cache blocks.

Aggregate admission is serialized by a process-local lock. Context creation
reserves its record before `calloc`; shadow-backed allocations reserve their
bytes before allocation. A staging block keeps its aggregate charge while it
moves between active use and the cache. Cache trimming and context destruction
release the charge as the corresponding storage is freed. A failed allocator
reservation releases its charge. Accounting underflow does not reduce the
aggregate counter. Staging-cache admission compares block capacity against the
remaining cache allowance with subtraction after validating the current
counter, so a damaged counter cannot wrap the room calculation.

This is accounting for RinGL-owned CPU memory routed through the context
budget. It does not account for GPU-driver allocations or the separate
Aquamarine surface and generic software-backend owner budgets. The broader
untrusted-allocation audit remains open for those owners and for hardware/QEMU
evidence.

Source inspection only was performed for these changes. No build or tests were
run, so compilation and runtime behavior remain unverified in this work session.
