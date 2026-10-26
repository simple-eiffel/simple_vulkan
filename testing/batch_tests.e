note
	description: "[
		Tests for compute batches (0.2.0): device-local buffers, address pipelines,
		recorded dispatches, copies and fills, flush; and the descriptor-set leak fix.
		Shaders are compiled at run time by simple_shaderc (test target only).
	]"
	author: "Larry Rix"

class
	BATCH_TESTS

create
	make

feature {NONE} -- Initialization

	make
		do
			create ctx.make
		end

feature -- Access

	ctx: VULKAN_CONTEXT
	passed, failed: INTEGER

feature -- Execution

	run_all
		do
			if ctx.is_valid and then ctx.has_device_address then
				check_that ("device buffer round trip (16 MB), offset read, fills", device_buffer_round_trip)
				check_that ("address pipeline: vector add of 1,000,003 floats", vector_add)
				check_that ("1,000 dependent dispatches in one batch run in order", batched_increments)
				check_that ("recorded copy between device buffers", recorded_copy)
				check_that ("100 descriptor pipelines created and freed (pool holds 32)", descriptor_sets_returned)
				check_that ("deferred release: queued while a batch is open, freed at flush", deferred_release)
				check_that ("pool: recycled buffer reused by size, reuse ordered after old use, limit and trim", pool_reuse)
				check_that ("profiling: 200 dispatches timed on the GPU, none when off", profiling)
			else
				print ("Test: compute batches... SKIP (no device addresses)%N")
			end
			ctx.dispose
		end

feature {NONE} -- Tests

	device_buffer_round_trip: BOOLEAN
		local
			b: VULKAN_DEVICE_BUFFER
			l_out, l_in: MANAGED_POINTER
			i, n: INTEGER
		do
			n := 4 * 1024 * 1024
			create b.make (ctx, n * 4)
			create l_out.make (n * 4)
			create l_in.make (n * 4)
			from i := 0 until i = n loop
				l_out.put_integer_32 (i * 7 - 3, i * 4)
				i := i + 1
			end
			Result := b.is_valid and b.address /= 0 and b.write (l_out.item, n * 4, 0) and b.read (l_in.item, n * 4, 0)
			Result := Result and l_in.item.memory_compare (l_out.item, n * 4)
				-- Offset read of 8 bytes from element 1000.
			Result := Result and b.read (l_in.item, 8, 4000) and l_in.read_integer_32 (0) = 6997 and l_in.read_integer_32 (4) = 7004
				-- Recorded fills: ones in the first 16 bytes, zeros after.
			Result := Result and b.record_fill (0, 16, 0x3F800000) and b.record_fill (16, 16, 0)
			Result := Result and b.read (l_in.item, 32, 0)
			Result := Result and l_in.read_real_32 (0) = 1.0 and l_in.read_real_32 (12) = 1.0 and l_in.read_integer_32 (16) = 0 and l_in.read_integer_32 (28) = 0
			b.dispose
		end

	vector_add: BOOLEAN
		local
			a, b, c: VULKAN_DEVICE_BUFFER
			p: VULKAN_ADDRESS_PIPELINE
			l_a, l_b, l_c, l_push: MANAGED_POINTER
			i, n: INTEGER
		do
			n := 1_000_003
			create l_a.make (n * 4); create l_b.make (n * 4); create l_c.make (n * 4)
			from i := 0 until i = n loop
				l_a.put_real_32 (i.to_real, i * 4)
				l_b.put_real_32 ((2 * i).to_real, i * 4)
				i := i + 1
			end
			create a.make (ctx, n * 4); create b.make (ctx, n * 4); create c.make (ctx, n * 4)
			if attached pipeline (Add_glsl) as l_p then
				p := l_p
				create l_push.make (28)
				l_push.put_natural_64 (a.address, 0)
				l_push.put_natural_64 (b.address, 8)
				l_push.put_natural_64 (c.address, 16)
				l_push.put_natural_32 (n.to_natural_32, 24)
				Result := a.write (l_a.item, n * 4, 0) and b.write (l_b.item, n * 4, 0)
					and p.record (l_push.item, 28, (n + 255) // 256, 1, 1)
					and ctx.pending_commands = 1
					and c.read (l_c.item, n * 4, 0)
				from i := 0 until not Result or i = n loop
					Result := l_c.read_real_32 (i * 4) = (3 * i).to_real
					i := i + 1
				end
				p.dispose
			end
			a.dispose; b.dispose; c.dispose
		end

	batched_increments: BOOLEAN
			-- x := x + 1 recorded 1,000 times over the same buffer: only correct if
			-- every barrier orders each dispatch after the previous one.
		local
			x: VULKAN_DEVICE_BUFFER
			p: VULKAN_ADDRESS_PIPELINE
			l_host, l_push: MANAGED_POINTER
			i, n: INTEGER
		do
			n := 65_536
			create x.make (ctx, n * 4)
			create l_host.make (n * 4)
			if attached pipeline (Increment_glsl) as l_p then
				p := l_p
				create l_push.make (12)
				l_push.put_natural_64 (x.address, 0)
				l_push.put_natural_32 (n.to_natural_32, 8)
				Result := x.record_fill (0, n * 4, 0)
				from i := 1 until not Result or i > 1000 loop
					Result := p.record (l_push.item, 12, n // 256, 1, 1)
					i := i + 1
				end
				Result := Result and ctx.pending_commands = 1001
				ctx.flush
				Result := Result and ctx.last_flush_succeeded and ctx.pending_commands = 0
				Result := Result and x.read (l_host.item, n * 4, 0)
				from i := 0 until not Result or i = n loop
					Result := l_host.read_real_32 (i * 4) = 1000.0
					i := i + 1
				end
				p.dispose
			end
			x.dispose
		end

	recorded_copy: BOOLEAN
		local
			a, b: VULKAN_DEVICE_BUFFER
			l_host: MANAGED_POINTER
		do
			create a.make (ctx, 64)
			create b.make (ctx, 64)
			create l_host.make (64)
			Result := a.record_fill (0, 64, 0x40400000) and b.record_fill (0, 64, 0)
				and a.record_copy_to (b, 16, 32, 16)
				and b.read (l_host.item, 64, 0)
			Result := Result and l_host.read_real_32 (28) = 0.0 and l_host.read_real_32 (32) = 3.0
				and l_host.read_real_32 (44) = 3.0 and l_host.read_real_32 (48) = 0.0
			a.dispose; b.dispose
		end

	descriptor_sets_returned: BOOLEAN
			-- Before 0.2.0 each descriptor pipeline took a set from a 32-set pool and
			-- never returned it, and a failed set allocation still yielded a "valid"
			-- pipeline. Now: 100 create/free cycles all succeed, and the last pipeline
			-- still dispatches (writes 7.0 into every element).
		local
			s: VULKAN_SHADER
			q: VULKAN_PIPELINE
			buf: VULKAN_BUFFER
			l_host: MANAGED_POINTER
			l_spirv: MANAGED_POINTER
			i: INTEGER
		do
			if attached (create {SIMPLE_SHADERC}.make).compile_compute (Legacy_glsl) as l_code then
				l_spirv := l_code
				create s.make_from_memory (ctx, l_spirv.item, l_spirv.count.to_integer_64)
				Result := s.is_valid
				from i := 1 until not Result or i > 99 loop
					create q.make (ctx, s)
					Result := q.is_valid
					q.dispose
					i := i + 1
				end
				create q.make (ctx, s)
				create buf.make (ctx, 256, {VULKAN_BUFFER}.Buffer_storage)
				create l_host.make (256)
				Result := Result and q.is_valid and buf.is_valid and q.bind_buffer (0, buf) and q.dispatch (ctx, 1, 1, 1)
					and buf.download (l_host.item, 256, 0)
				from i := 0 until not Result or i = 64 loop
					Result := l_host.read_real_32 (i * 4) = 7.0
					i := i + 1
				end
				q.dispose
				buf.dispose
				s.dispose
			end
		end

	deferred_release: BOOLEAN
			-- A buffer released while a recorded dispatch still writes it is queued, the
			-- dispatch still runs (the result copied out lands), and the flush frees it.
		local
			x, keep: VULKAN_DEVICE_BUFFER
			p: VULKAN_ADDRESS_PIPELINE
			l_push, l_host: MANAGED_POINTER
		do
			create x.make (ctx, 1024)
			create keep.make (ctx, 1024)
			create l_host.make (1024)
			if attached pipeline (Increment_glsl) as l_p then
				p := l_p
				create l_push.make (12)
				l_push.put_natural_64 (x.address, 0)
				l_push.put_natural_32 (256, 8)
				Result := x.record_fill (0, 1024, 0) and p.record (l_push.item, 12, 1, 1, 1)
					and x.record_copy_to (keep, 0, 0, 1024)
				x.release_deferred
				Result := Result and not x.is_valid and ctx.queued_frees = 1
				ctx.flush
				Result := Result and ctx.queued_frees = 0 and keep.read (l_host.item, 1024, 0)
					and l_host.read_real_32 (0) = 1.0 and l_host.read_real_32 (1020) = 1.0
				x.release_deferred
				Result := Result and ctx.queued_frees = 0
				p.dispose
			end
			keep.dispose
		end

	pool_reuse: BOOLEAN
			-- A buffer recycled while a recorded dispatch still writes it is handed to the next
			-- same-size `make'; that new owner's fill is ordered after the old dispatch, so a
			-- read shows the fill, not the old increments. Other sizes allocate fresh.
		local
			x, y, z, keep: VULKAN_DEVICE_BUFFER
			p: VULKAN_ADDRESS_PIPELINE
			l_push, l_host: MANAGED_POINTER
			l_hits: INTEGER_64
		do
			ctx.trim_pool
			ctx.flush
			create x.make (ctx, 4096)
			create keep.make (ctx, 4096)
			create l_host.make (4096)
			if attached pipeline (Increment_glsl) as l_p then
				p := l_p
				create l_push.make (12)
				l_push.put_natural_64 (x.address, 0)
				l_push.put_natural_32 (1024, 8)
				l_hits := ctx.pool_hits
				Result := x.record_fill (0, 4096, 0) and p.record (l_push.item, 12, 4, 1, 1)
					and x.record_copy_to (keep, 0, 0, 4096)
				x.recycle
				Result := Result and ctx.pooled_bytes = 4096
				create y.make (ctx, 4096)
				Result := Result and ctx.pool_hits = l_hits + 1 and ctx.pooled_bytes = 0
				Result := Result and y.record_fill (0, 4096, 0x40A00000) and y.read (l_host.item, 4096, 0)
					and l_host.read_real_32 (0) = 5.0 and l_host.read_real_32 (4092) = 5.0
				Result := Result and keep.read (l_host.item, 4096, 0) and l_host.read_real_32 (0) = 1.0
				create z.make (ctx, 8192)
				Result := Result and ctx.pool_hits = l_hits + 1
				y.recycle
				z.recycle
				Result := Result and ctx.pooled_bytes = 4096 + 8192
				ctx.set_pool_limit (5000)
				Result := Result and ctx.pooled_bytes <= 5000
				ctx.trim_pool
				Result := Result and ctx.pooled_bytes = 0
				ctx.set_pool_limit (1024 * 1024 * 1024)
				p.dispose
			end
			keep.dispose
		end

	profiling: BOOLEAN
			-- 200 timed increments over 4M floats: 200 calls and a positive GPU time; with
			-- profiling off, 50 more dispatches add nothing.
		local
			x: VULKAN_DEVICE_BUFFER
			p: VULKAN_ADDRESS_PIPELINE
			l_push: MANAGED_POINTER
			i, n: INTEGER
			l_ms: REAL_64
		do
			n := 4 * 1024 * 1024
			create x.make (ctx, n * 4)
			if attached pipeline (Increment_glsl) as l_p then
				p := l_p
				create l_push.make (12)
				l_push.put_natural_64 (x.address, 0)
				l_push.put_natural_32 (n.to_natural_32, 8)
				ctx.set_profiling (True)
				Result := ctx.is_profiling
				from i := 1 until not Result or i > 200 loop
					Result := p.record (l_push.item, 12, n // 256, 1, 1)
					i := i + 1
				end
				ctx.flush
				l_ms := p.profiled_ms
				print ("(200 dispatches: " + l_ms.out + " ms GPU) ")
				Result := Result and p.profiled_calls = 200 and l_ms > 0.0
				ctx.set_profiling (False)
				from i := 1 until not Result or i > 50 loop
					Result := p.record (l_push.item, 12, n // 256, 1, 1)
					i := i + 1
				end
				ctx.flush
				Result := Result and p.profiled_calls = 200
				p.reset_profile
				Result := Result and p.profiled_calls = 0
				p.dispose
			end
			x.dispose
		end

feature {NONE} -- Helpers

	pipeline (a_glsl: STRING): detachable VULKAN_ADDRESS_PIPELINE
			-- Address pipeline for `a_glsl', or Void when compilation fails.
		local
			s: VULKAN_SHADER
		do
			if attached (create {SIMPLE_SHADERC}.make).compile_compute (a_glsl) as l_code then
				create s.make_from_memory (ctx, l_code.item, l_code.count.to_integer_64)
				if s.is_valid then
					create Result.make (ctx, s)
					s.dispose
				end
			else
				print ("  (shader compile failed)%N")
			end
		end

	check_that (a_name: STRING; a_ok: BOOLEAN)
		do
			print ("Test: " + a_name + "... ")
			if a_ok then
				print ("PASS%N")
				passed := passed + 1
			else
				print ("FAIL%N")
				failed := failed + 1
			end
		end

feature {NONE} -- Shaders

	Add_glsl: STRING = "[
		#version 460
		#extension GL_EXT_buffer_reference : require
		layout(local_size_x = 256) in;
		layout(buffer_reference, std430) buffer Floats { float v[]; };
		layout(push_constant) uniform P { Floats a; Floats b; Floats c; uint n; } p;
		void main() {
			uint i = gl_GlobalInvocationID.x;
			if (i < p.n) p.c.v[i] = p.a.v[i] + p.b.v[i];
		}
	]"

	Increment_glsl: STRING = "[
		#version 460
		#extension GL_EXT_buffer_reference : require
		layout(local_size_x = 256) in;
		layout(buffer_reference, std430) buffer Floats { float v[]; };
		layout(push_constant) uniform P { Floats x; uint n; } p;
		void main() {
			uint i = gl_GlobalInvocationID.x;
			if (i < p.n) p.x.v[i] = p.x.v[i] + 1.0;
		}
	]"

	Legacy_glsl: STRING = "[
		#version 450
		layout(local_size_x = 64) in;
		layout(std430, binding = 0) buffer B { float v[]; };
		void main() { v[gl_GlobalInvocationID.x] = 7.0; }
	]"

end
