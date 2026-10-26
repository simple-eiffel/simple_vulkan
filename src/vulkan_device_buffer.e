note
	description: "[
		VULKAN_DEVICE_BUFFER - a buffer in GPU-only (device-local) memory that
		shaders reach by its 64-bit `address' (GL_EXT_buffer_reference), passed
		to a VULKAN_ADDRESS_PIPELINE in push constants. No descriptor sets.

		`write' and `read' go through the context's staging buffer and first run
		everything recorded (so a read sees every recorded dispatch). `record_copy'
		and `record_fill' are recorded into the context's open batch and run at
		the next `flush', `read' or `write'. Transfers are blocking externals.

		Usage:
			create buf.make (ctx, 4 * n)
			ok := buf.write (host.item, 4 * n, 0)
			-- record dispatches that use buf.address ...
			ok := buf.read (host.item, 4 * n, 0)
			buf.dispose
	]"
	author: "Larry Rix"

class
	VULKAN_DEVICE_BUFFER

create
	make

feature {NONE} -- Initialization

	make (a_ctx: VULKAN_CONTEXT; a_size: INTEGER_64)
			-- Device-local buffer of `a_size' bytes.
		require
			ctx_valid: a_ctx.is_valid
			addressable: a_ctx.has_device_address
			positive_size: a_size > 0
		do
			context := a_ctx
			size := a_size
			handle := svk_create_device_buffer (a_ctx.handle, a_size.to_natural_64)
			is_valid := handle /= default_pointer
			if is_valid then
				address := svk_buffer_address (a_ctx.handle, handle)
			end
		ensure
			context_set: context = a_ctx
			size_set: size = a_size
			addressed: is_valid implies address /= 0
		end

feature -- Access

	handle: POINTER
			-- Opaque svk_buffer.

	context: VULKAN_CONTEXT

	size: INTEGER_64
			-- Bytes.

	address: NATURAL_64
			-- Device address of byte 0, for shaders.

	is_valid: BOOLEAN
			-- Was the buffer created (and not yet disposed)?

feature -- Transfer (run all recorded work first)

	write (a_data: POINTER; a_size, a_offset: INTEGER_64): BOOLEAN
			-- Copy `a_size' bytes from host memory `a_data' to byte `a_offset'.
		require
			valid: is_valid
			data_attached: a_data /= default_pointer
			positive_size: a_size > 0
			in_range: a_offset >= 0 and then a_offset + a_size <= size
		do
			Result := svk_write_buffer (context.handle, handle, a_data, a_size.to_natural_64, a_offset.to_natural_64) /= 0
		ensure
			flushed: Result implies context.pending_commands = 0
		end

	read (a_data: POINTER; a_size, a_offset: INTEGER_64): BOOLEAN
			-- Copy `a_size' bytes at byte `a_offset' to host memory `a_data'.
		require
			valid: is_valid
			data_attached: a_data /= default_pointer
			positive_size: a_size > 0
			in_range: a_offset >= 0 and then a_offset + a_size <= size
		do
			Result := svk_read_buffer (context.handle, handle, a_data, a_size.to_natural_64, a_offset.to_natural_64) /= 0
		ensure
			flushed: Result implies context.pending_commands = 0
		end

feature -- Recording (runs at the next flush)

	record_copy_to (a_target: VULKAN_DEVICE_BUFFER; a_source_offset, a_target_offset, a_size: INTEGER_64): BOOLEAN
			-- Record a copy of `a_size' bytes into `a_target'.
		require
			valid: is_valid and a_target.is_valid
			same_context: a_target.context = context
			positive_size: a_size > 0
			source_range: a_source_offset >= 0 and then a_source_offset + a_size <= size
			target_range: a_target_offset >= 0 and then a_target_offset + a_size <= a_target.size
		do
			Result := svk_record_copy (context.handle, handle, a_source_offset.to_natural_64,
				a_target.handle, a_target_offset.to_natural_64, a_size.to_natural_64) /= 0
		end

	record_fill (a_offset, a_size: INTEGER_64; a_word: NATURAL_32): BOOLEAN
			-- Record filling `a_size' bytes from `a_offset' with the 32-bit `a_word'
			-- (0 gives zeros; {REAL_32} 1.0 is 0x3F800000).
		require
			valid: is_valid
			aligned: a_offset \\ 4 = 0 and a_size \\ 4 = 0
			positive_size: a_size > 0
			in_range: a_offset >= 0 and then a_offset + a_size <= size
		do
			Result := svk_record_fill (context.handle, handle, a_offset.to_natural_64, a_size.to_natural_64, a_word) /= 0
		end

feature -- Disposal

	dispose
			-- Free the GPU memory (runs recorded work first: it may use this buffer).
		do
			if is_valid then
				svk_free_buffer (context.handle, handle)
				handle := default_pointer
				is_valid := False
				address := 0
			end
		ensure
			disposed: not is_valid and handle = default_pointer
		end

	release_deferred
			-- Free the GPU memory without waiting: now when no batch is open, else
			-- after the next flush's fence. Safe from a garbage collector's `dispose'.
		do
			if is_valid then
				svk_free_buffer_deferred (context.handle, handle)
				handle := default_pointer
				is_valid := False
				address := 0
			end
		ensure
			released: not is_valid and handle = default_pointer
		end

	recycle
			-- Give the buffer to the context's pool for reuse by a later `make' of the same
			-- size (0.3.0); beyond the pool limit it is freed (deferred). Safe at once and from
			-- a collector's `dispose'. The contents are not kept.
		do
			if is_valid then
				svk_recycle_buffer (context.handle, handle)
				handle := default_pointer
				is_valid := False
				address := 0
			end
		ensure
			recycled: not is_valid and handle = default_pointer
		end

feature {NONE} -- C externals

	svk_recycle_buffer (ctx, buf: POINTER)
		external
			"C inline use <simple_vulkan.h>"
		alias
			"svk_recycle_buffer((svk_context)$ctx, (svk_buffer)$buf);"
		end

	svk_free_buffer_deferred (ctx, buf: POINTER)
		external
			"C inline use <simple_vulkan.h>"
		alias
			"svk_free_buffer_deferred((svk_context)$ctx, (svk_buffer)$buf);"
		end

	svk_create_device_buffer (ctx: POINTER; a_size: NATURAL_64): POINTER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_create_device_buffer((svk_context)$ctx, (uint64_t)$a_size);"
		end

	svk_buffer_address (ctx, buf: POINTER): NATURAL_64
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return (EIF_NATURAL_64) svk_buffer_address((svk_context)$ctx, (svk_buffer)$buf);"
		end

	svk_write_buffer (ctx, buf, data: POINTER; a_size, a_offset: NATURAL_64): INTEGER
		external
			"C blocking inline use <simple_vulkan.h>"
		alias
			"return svk_write_buffer((svk_context)$ctx, (svk_buffer)$buf, $data, (uint64_t)$a_size, (uint64_t)$a_offset);"
		end

	svk_read_buffer (ctx, buf, data: POINTER; a_size, a_offset: NATURAL_64): INTEGER
		external
			"C blocking inline use <simple_vulkan.h>"
		alias
			"return svk_read_buffer((svk_context)$ctx, (svk_buffer)$buf, $data, (uint64_t)$a_size, (uint64_t)$a_offset);"
		end

	svk_record_copy (ctx, src: POINTER; a_src_offset: NATURAL_64; dst: POINTER; a_dst_offset, a_size: NATURAL_64): INTEGER
		external
			"C blocking inline use <simple_vulkan.h>"
		alias
			"return svk_record_copy((svk_context)$ctx, (svk_buffer)$src, (uint64_t)$a_src_offset, (svk_buffer)$dst, (uint64_t)$a_dst_offset, (uint64_t)$a_size);"
		end

	svk_record_fill (ctx, buf: POINTER; a_offset, a_size: NATURAL_64; a_word: NATURAL_32): INTEGER
		external
			"C blocking inline use <simple_vulkan.h>"
		alias
			"return svk_record_fill((svk_context)$ctx, (svk_buffer)$buf, (uint64_t)$a_offset, (uint64_t)$a_size, (uint32_t)$a_word);"
		end

	svk_free_buffer (ctx, buf: POINTER)
		external
			"C blocking inline use <simple_vulkan.h>"
		alias
			"svk_free_buffer((svk_context)$ctx, (svk_buffer)$buf);"
		end

invariant
	valid_handle: is_valid implies handle /= default_pointer

end
