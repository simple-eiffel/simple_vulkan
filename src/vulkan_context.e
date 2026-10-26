note
	description: "[
		VULKAN_CONTEXT - Vulkan device context wrapper.

		Represents an initialized Vulkan instance with a selected physical
		device (GPU) and logical device. Automatically selects the best
		available discrete GPU, falling back to integrated graphics.

		Usage:
			local
				ctx: VULKAN_CONTEXT
			do
				create ctx.make
				if ctx.is_valid then
					print (ctx.device_name)
					print (ctx.is_discrete_gpu)
					-- ... use context ...
					ctx.dispose
				end
			end
	]"
	author: "Larry Rix"
	date: "$Date$"
	revision: "$Revision$"

class
	VULKAN_CONTEXT

create
	make

feature {NONE} -- Initialization

	make
			-- Initialize Vulkan and select best available GPU.
		do
			handle := svk_init
			is_valid := handle /= default_pointer
		ensure
			valid_implies_handle: is_valid implies handle /= default_pointer
		end

feature -- Access

	handle: POINTER
			-- Opaque handle to svk_context

	is_valid: BOOLEAN
			-- Was initialization successful?

	device_name: STRING
			-- GPU device name (e.g., "NVIDIA GeForce RTX 5070 Ti")
		require
			valid: is_valid
		local
			c_str: POINTER
			l_c_string: C_STRING
		do
			c_str := svk_get_device_name (handle)
			if c_str /= default_pointer then
				create l_c_string.make_by_pointer (c_str)
				Result := l_c_string.string
			else
				Result := "Unknown"
			end
		ensure
			result_attached: Result /= Void
		end

	vendor_id: INTEGER
			-- GPU vendor ID (0x10DE=NVIDIA, 0x1002=AMD, 0x8086=Intel)
		require
			valid: is_valid
		do
			Result := svk_get_vendor_id (handle).to_integer_32
		end

	is_discrete_gpu: BOOLEAN
			-- Is this a discrete GPU (vs integrated)?
		require
			valid: is_valid
		do
			Result := svk_is_discrete_gpu (handle) /= 0
		end

	max_workgroup_size: INTEGER
			-- Maximum compute workgroup size (typically 256-1024)
		require
			valid: is_valid
		do
			Result := svk_get_max_workgroup_size (handle).to_integer_32
		end

feature -- Vendor Constants

	Vendor_nvidia: INTEGER = 0x10DE
			-- NVIDIA vendor ID

	Vendor_amd: INTEGER = 0x1002
			-- AMD vendor ID

	Vendor_intel: INTEGER = 0x8086
			-- Intel vendor ID

feature -- Status

	is_nvidia: BOOLEAN
			-- Is this an NVIDIA GPU?
		require
			valid: is_valid
		do
			Result := vendor_id = Vendor_nvidia
		end

	is_amd: BOOLEAN
			-- Is this an AMD GPU?
		require
			valid: is_valid
		do
			Result := vendor_id = Vendor_amd
		end

	is_intel: BOOLEAN
			-- Is this an Intel GPU?
		require
			valid: is_valid
		do
			Result := vendor_id = Vendor_intel
		end

feature -- Compute batches (0.2.0)

	has_device_address: BOOLEAN
			-- Can shaders address device-local buffers (VULKAN_DEVICE_BUFFER,
			-- VULKAN_ADDRESS_PIPELINE)? Vulkan 1.2 bufferDeviceAddress.
		require
			valid: is_valid
		do
			Result := svk_has_device_address (handle) /= 0
		end

	has_cooperative_matrix_bf16: BOOLEAN
			-- Are bf16 cooperative matrices (tensor cores) enabled: Vulkan 1.3 kernels
			-- (SPIR-V 1.6) may use 16x16x16 subgroup tiles of bf16 A and B with f32
			-- accumulators (0.6.0)?
		require
			valid: is_valid
		do
			Result := svk_has_cooperative_matrix_bf16 (handle) /= 0
		end

	pending_commands: INTEGER
			-- Dispatches, copies and fills recorded since the last `flush'.
		require
			valid: is_valid
		do
			Result := svk_pending_commands (handle).to_integer_32
		end

	flush
			-- Run everything recorded and wait for the GPU to finish it.
			-- (A blocking external: another SCOOP processor's collector never waits on it.)
		require
			valid: is_valid
		do
			last_flush_succeeded := svk_flush (handle) /= 0
		ensure
			nothing_pending: last_flush_succeeded implies pending_commands = 0
		end

	last_flush_succeeded: BOOLEAN
			-- Did the last `flush' submit and complete?

	queued_frees: INTEGER
			-- Device buffers released while a batch was open, freed at the next flush.
		require
			valid: is_valid
		do
			Result := svk_queued_frees (handle).to_integer_32
		end

feature -- Profiling (0.4.0)

	is_profiling: BOOLEAN
			-- Are recorded dispatches being timed on the GPU?

	set_profiling (a_on: BOOLEAN)
			-- Time every recorded dispatch (GPU timestamps before and after) and add the
			-- elapsed time to its pipeline at each flush. Runs any open batch first.
		require
			valid: is_valid
		do
			if svk_set_profiling (handle, a_on.to_integer) /= 0 then
				is_profiling := a_on
			end
		ensure
			nothing_pending: pending_commands = 0
		end

feature -- Device-buffer pool (0.3.0)

	pooled_bytes: INTEGER_64
			-- Bytes held by recycled device buffers awaiting reuse.
		require
			valid: is_valid
		do
			Result := svk_pool_bytes (handle).to_integer_64
		end

	pool_hits: INTEGER_64
			-- Device-buffer creations served from the pool so far.
		require
			valid: is_valid
		do
			Result := svk_pool_hits (handle).to_integer_64
		end

	set_pool_limit (a_bytes: INTEGER_64)
			-- Keep at most `a_bytes' in the pool (default 1 GiB); frees the excess.
		require
			valid: is_valid
			non_negative: a_bytes >= 0
		do
			svk_set_pool_limit (handle, a_bytes.to_natural_64)
		ensure
			within: pooled_bytes <= a_bytes
		end

	trim_pool
			-- Free every pooled buffer (deferred while a batch is open).
		require
			valid: is_valid
		do
			svk_trim_pool (handle)
		ensure
			empty: pooled_bytes = 0
		end

feature -- Disposal

	dispose
			-- Release Vulkan resources.
		do
			if is_valid and handle /= default_pointer then
				svk_cleanup (handle)
				handle := default_pointer
				is_valid := False
			end
		ensure
			disposed: not is_valid
			handle_cleared: handle = default_pointer
		end

feature {NONE} -- C Externals

	svk_init: POINTER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_init();"
		end

	svk_get_device_name (ctx: POINTER): POINTER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return (char*)svk_get_device_name((svk_context)$ctx);"
		end

	svk_get_vendor_id (ctx: POINTER): NATURAL_32
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_get_vendor_id((svk_context)$ctx);"
		end

	svk_is_discrete_gpu (ctx: POINTER): INTEGER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_is_discrete_gpu((svk_context)$ctx);"
		end

	svk_get_max_workgroup_size (ctx: POINTER): NATURAL_32
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_get_max_workgroup_size((svk_context)$ctx);"
		end

	svk_cleanup (ctx: POINTER)
		external
			"C blocking inline use <simple_vulkan.h>"
		alias
			"svk_cleanup((svk_context)$ctx);"
		end

	svk_has_device_address (ctx: POINTER): INTEGER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_has_device_address((svk_context)$ctx);"
		end

	svk_has_cooperative_matrix_bf16 (ctx: POINTER): INTEGER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_has_cooperative_matrix_bf16((svk_context)$ctx);"
		end

	svk_pending_commands (ctx: POINTER): NATURAL_32
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_pending_commands((svk_context)$ctx);"
		end

	svk_set_profiling (ctx: POINTER; a_on: INTEGER): INTEGER
		external
			"C blocking inline use <simple_vulkan.h>"
		alias
			"return svk_set_profiling((svk_context)$ctx, (int)$a_on);"
		end

	svk_pool_bytes (ctx: POINTER): NATURAL_64
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return (EIF_NATURAL_64) svk_pool_bytes((svk_context)$ctx);"
		end

	svk_pool_hits (ctx: POINTER): NATURAL_64
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return (EIF_NATURAL_64) svk_pool_hits((svk_context)$ctx);"
		end

	svk_set_pool_limit (ctx: POINTER; a_bytes: NATURAL_64)
		external
			"C inline use <simple_vulkan.h>"
		alias
			"svk_set_pool_limit((svk_context)$ctx, (uint64_t)$a_bytes);"
		end

	svk_trim_pool (ctx: POINTER)
		external
			"C inline use <simple_vulkan.h>"
		alias
			"svk_trim_pool((svk_context)$ctx);"
		end

	svk_queued_frees (ctx: POINTER): NATURAL_32
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_queued_frees((svk_context)$ctx);"
		end

	svk_flush (ctx: POINTER): INTEGER
		external
			"C blocking inline use <simple_vulkan.h>"
		alias
			"return svk_flush((svk_context)$ctx);"
		end

invariant
	valid_handle: is_valid implies handle /= default_pointer

end
