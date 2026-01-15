note
	description: "[
		VULKAN_BUFFER - GPU buffer wrapper for Vulkan compute.

		Manages GPU memory for shader storage buffers (SSBO),
		uniform buffers, and transfer operations. Supports
		upload/download between CPU and GPU memory.

		Usage:
			local
				buf: VULKAN_BUFFER
				data: MANAGED_POINTER
			do
				create buf.make (ctx, 1024, Buffer_storage)
				if buf.is_valid then
					create data.make (1024)
					-- ... fill data ...
					buf.upload (data.item, 1024, 0)
					-- ... dispatch compute shader ...
					buf.download (data.item, 1024, 0)
					buf.dispose
				end
			end
	]"
	author: "Larry Rix"
	date: "$Date$"
	revision: "$Revision$"

class
	VULKAN_BUFFER

create
	make

feature {NONE} -- Initialization

	make (a_ctx: VULKAN_CONTEXT; a_size: INTEGER_64; a_usage: INTEGER)
			-- Create GPU buffer with specified size and usage flags.
		require
			ctx_valid: a_ctx /= Void and then a_ctx.is_valid
			positive_size: a_size > 0
		do
			context := a_ctx
			size := a_size
			usage := a_usage
			handle := svk_create_buffer (a_ctx.handle, a_size.to_natural_64, a_usage.to_natural_32)
			is_valid := handle /= default_pointer
		ensure
			context_set: context = a_ctx
			size_set: size = a_size
			usage_set: usage = a_usage
		end

feature -- Access

	handle: POINTER
			-- Opaque handle to svk_buffer

	context: VULKAN_CONTEXT
			-- Parent context

	size: INTEGER_64
			-- Buffer size in bytes

	usage: INTEGER
			-- Buffer usage flags

	is_valid: BOOLEAN
			-- Was buffer creation successful?

feature -- Usage Flags

	Buffer_storage: INTEGER = 0x01
			-- Shader storage buffer (SSBO)

	Buffer_uniform: INTEGER = 0x02
			-- Uniform buffer

	Buffer_transfer: INTEGER = 0x04
			-- Transfer source/destination

feature -- Data Transfer

	upload (a_data: POINTER; a_size: INTEGER_64; a_offset: INTEGER_64): BOOLEAN
			-- Upload data from CPU to GPU buffer.
		require
			valid: is_valid
			data_attached: a_data /= default_pointer
			valid_size: a_size > 0
			valid_range: a_offset >= 0 and then a_offset + a_size <= size
		do
			Result := svk_upload_buffer (context.handle, handle, a_data,
				a_size.to_natural_64, a_offset.to_natural_64) /= 0
		end

	download (a_data: POINTER; a_size: INTEGER_64; a_offset: INTEGER_64): BOOLEAN
			-- Download data from GPU to CPU buffer.
		require
			valid: is_valid
			data_attached: a_data /= default_pointer
			valid_size: a_size > 0
			valid_range: a_offset >= 0 and then a_offset + a_size <= size
		do
			Result := svk_download_buffer (context.handle, handle, a_data,
				a_size.to_natural_64, a_offset.to_natural_64) /= 0
		end

feature -- Disposal

	dispose
			-- Free GPU buffer.
		do
			if is_valid and handle /= default_pointer then
				svk_free_buffer (context.handle, handle)
				handle := default_pointer
				is_valid := False
			end
		ensure
			disposed: not is_valid
			handle_cleared: handle = default_pointer
		end

feature {NONE} -- C Externals

	svk_create_buffer (ctx: POINTER; a_size: NATURAL_64; a_usage: NATURAL_32): POINTER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_create_buffer((svk_context)$ctx, (uint64_t)$a_size, (uint32_t)$a_usage);"
		end

	svk_upload_buffer (ctx, buf, data: POINTER; a_size, a_offset: NATURAL_64): INTEGER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_upload_buffer((svk_context)$ctx, (svk_buffer)$buf, $data, (uint64_t)$a_size, (uint64_t)$a_offset);"
		end

	svk_download_buffer (ctx, buf, data: POINTER; a_size, a_offset: NATURAL_64): INTEGER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_download_buffer((svk_context)$ctx, (svk_buffer)$buf, $data, (uint64_t)$a_size, (uint64_t)$a_offset);"
		end

	svk_buffer_size (buf: POINTER): NATURAL_64
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_buffer_size((svk_buffer)$buf);"
		end

	svk_free_buffer (ctx, buf: POINTER)
		external
			"C inline use <simple_vulkan.h>"
		alias
			"svk_free_buffer((svk_context)$ctx, (svk_buffer)$buf);"
		end

invariant
	valid_handle: is_valid implies handle /= default_pointer
	context_attached: context /= Void

end
