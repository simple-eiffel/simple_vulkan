# Changelog: simple_vulkan

## 0.8.0 (2026-10-26): device-buffer pool by size class
- **Before:** the pool matched recycled buffers by exact size, with a linear scan. A decoder's attention buffers grow by one position per token, so nearly every size was new: the pool filled to its limit with sizes never asked for again, and then every new size went to the driver while each allocation scanned the whole stale list. Needle 3 decoding fell from about 72 ms to 1.2-1.7 s per token once the pool reached 8 GB.
- **Now:** a device buffer is allocated at its size class: 256 bytes, then four classes per power of two (at most 25% over the request). The pool keeps one free list per class, so taking and recycling are O(1), and growing shapes reuse buffers.
  - `VULKAN_DEVICE_BUFFER.size` stays the size asked for (bounds checks and descriptor ranges use it); the allocation behind it may be larger.
  - `VULKAN_CONTEXT.pooled_bytes` counts allocated bytes. `set_pool_limit` and `trim_pool` free the largest classes first.
- Test `pool_size_classes`. 19/19.

## 0.7.0 (2026-10-26): asynchronous submission
- **Before:** recorded work ran only at a flush, which submitted and then waited. A training step's whole recording happened while the GPU sat idle, and then the host sat idle while the GPU ran it.
- **Now:** recording uses a ring of 3 slots. Each slot has its own command buffer, fence, upload arena, deferred-free list and timestamp queries.
  - Every 128 commands, the open slot is submitted without waiting, and recording moves on, so host and GPU work at once.
  - A slot is reused only after its fence.
  - `flush` keeps its meaning: it submits the open slot and waits for every slot.
- **Order:** commands still run in recorded order across slots. Each command's full barrier also scopes earlier and later submissions on the queue, so slots complete in order.
- **Frees:** a buffer released while work may use it is destroyed after that work completes. It waits behind the open slot, or behind the last submitted slot still in flight.
- **Synchronous paths** (upload, download, free, descriptor dispatch, pipeline free) now always flush. Work may be in flight with no slot open.
- **`pending_commands`** counts commands recorded since the last flush, some of which may already have run. The new `slots_in_flight` counts submissions not yet waited on. `flush` ensures `slots_in_flight = 0`.
- **Barriers:** the barrier after an ordinary command now covers device work only (compute and transfer). The host-visible barrier stays on the copies a read waits on.
- **Counters:** `flush_count` counts flushes that waited on work, and `wait_ms` is the host's total time waiting on fences.
- **Effect on RixGPT training** (simple_tensor): f32 334 → 317 ms/step, mixed 288 → 269 ms/step. The host now waits on the GPU for about 91% of a step, so the step is GPU-bound.
  - What remains is per-dispatch time on the GPU: about 1,700 barrier-separated dispatches per step. The cure is fusing ops into fewer dispatches.
  - Submitting every 32 commands instead of 128 changed nothing measurable.
- **Test:**
  - A fill and 400 dependent increments go out in slots while recording continues, with the ring wrapping. Every element ends at 400.
  - A buffer released while its slots are in flight is queued, both with a slot open and with none open, and is freed by the flush.

## 0.6.0 (2026-10-26): bf16 cooperative matrices (tensor cores)
- The instance asks for Vulkan 1.3, so kernels compiled for SPIR-V 1.6 can run.
- The device enables bf16 cooperative matrices only when it offers the whole set. Otherwise nothing extra is enabled.
  - The set: Vulkan 1.3, VK_KHR_cooperative_matrix, VK_KHR_shader_bfloat16 (bf16 type and bf16 cooperative matrices), the Vulkan memory model, and a 16×16×16 subgroup shape with bf16 A and B and f32 C and result.
- `VULKAN_CONTEXT.has_cooperative_matrix_bf16` says whether it did.
- Test: one subgroup multiplies two 16×16 bf16 tiles on the tensor cores. The f32 result equals the host's exactly; the inputs are small integers, which bf16 holds exactly. It is skipped on devices without the feature. 17/17; simple_sdf 26/26.
- Shaders using it are compiled with simple_shaderc 0.2.0's `set_target_vulkan_1_3`.

## 0.5.0 (2026-10-26): recorded uploads
- `VULKAN_DEVICE_BUFFER.record_write`: copies host data into a per-batch upload arena now and records its copy into the batch, ordered with the other commands, without flushing. The arena has up to 64 host-visible chunks of at least 4 MB and is reset after each flush's fence; if it is full, the call falls back to `write`. Small per-step uploads (token ids, targets, gather layouts) no longer drain the GPU.
- Test: three payloads (two small, one of 8 MB) recorded into one batch, with the host buffer reused between them, and a dispatch reading one; nothing flushes until the read, and every buffer holds its own payload. 16/16.

## 0.4.0 (2026-10-26): GPU profiling
- `VULKAN_CONTEXT.set_profiling`: with profiling on, every recorded dispatch is bracketed by GPU timestamps (a query pool sized for a full batch), and each flush adds the elapsed time to its pipeline.
- `VULKAN_ADDRESS_PIPELINE`: `profiled_ms`, `profiled_calls`, `reset_profile`.
- Test: 200 timed dispatches give 200 calls and a positive GPU time; dispatches recorded with profiling off add nothing. 15/15.

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
