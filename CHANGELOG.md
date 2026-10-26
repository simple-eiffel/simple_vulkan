# Changelog: simple_vulkan

## 0.3.0 (2026-10-26): device-buffer pool
- `VULKAN_DEVICE_BUFFER.recycle` hands a buffer to the context's pool, and the next `make` of the same byte size reuses it, skipping the driver allocation. Reuse is safe at once: commands run in recorded order with a barrier after each, so the new first use follows the old last use. `recycle` is safe from a garbage collector's `dispose`.
- `VULKAN_CONTEXT`: `pooled_bytes`, `pool_hits`, `set_pool_limit` (default 1 GiB; bytes beyond it are freed, deferred) and `trim_pool`.
- Test: a buffer recycled while a dispatch still writes it is reused, and the new owner's fill lands after the old dispatch; other sizes allocate fresh; the limit and trim are checked. 14/14.

## 0.2.0 (2026-10-26): compute batches
Gap G-4 for simple_tensor's GPU backend.

### Added
- **VULKAN_DEVICE_BUFFER:** device-local memory with a 64-bit shader `address`.
  - `write` and `read` go through a staging buffer, after running all recorded work.
  - `record_copy_to` and `record_fill` are recorded into the batch.
  - `release_deferred` frees without waiting: at once when no batch is open, otherwise after the next flush. It is safe to call from a garbage collector's `dispose`.
- **VULKAN_ADDRESS_PIPELINE:** a pipeline with no descriptor sets. Shaders read buffers by address (GL_EXT_buffer_reference) from up to 128 bytes of push constants; `record` adds a dispatch to the batch.
- **VULKAN_CONTEXT:** `has_device_address`, `flush`, `pending_commands`, `queued_frees`. Every recorded command is followed by a full compute/transfer memory barrier. A batch flushes automatically at 16,384 commands.
- **Device setup:** the device is created with `bufferDeviceAddress` when it supports it (Vulkan 1.2).
- **Build:** `build_clib.sh` builds the C library from Git Bash when cl.exe, INCLUDE and LIB are set.

### Fixed
- `svk_free_pipeline` now returns its descriptor set to the pool. Before, each descriptor pipeline used up one of the pool's 32 sets for good.
- `svk_create_pipeline` now fails when it can't allocate a descriptor set. Before, it returned a pipeline with no set.
- Legacy `dispatch`, `upload`, `download` and `free` flush any recorded batch first, so queue order is kept.
- `svk_cleanup` is a `blocking` external.

### Tests
- Six dispatching tests (the library had none):
  - a 16 MB device round trip with an offset read and fills;
  - a vector add of 1,000,003 floats;
  - 1,000 dependent dispatches in one batch;
  - a recorded copy;
  - 100 descriptor pipelines created and freed, then a dispatch through the last one;
  - a deferred release while a dispatch still uses the buffer.
- Mutation-checked: removing the barriers fails the ordering test; removing the descriptor-set free fails the pool test.

### Measured (RTX 5070 Ti, spike S-5 in simple_tensor)
- 1,000 small dispatches: 6 ms batched, against 74 ms with a submit and wait each.
- Tiled matmul on device-local buffers: 3,616 GFLOP/s at 2048³, against 332 on host-visible buffers.

## 0.1.0
- Context, buffers, images, shaders, descriptor pipelines, push constants, SDF ray-marching helper.
