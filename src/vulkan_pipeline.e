note
	description: "[
		VULKAN_PIPELINE - Compute pipeline wrapper for Vulkan.

		Represents a complete compute pipeline with shader, descriptor
		bindings, and push constants. Bind buffers and images to
		descriptor slots, then dispatch compute workgroups.

		Usage:
			local
				pipe: VULKAN_PIPELINE
			do
				create pipe.make (ctx, shader)
				if pipe.is_valid then
					pipe.bind_buffer (0, input_buffer)
					pipe.bind_buffer (1, output_buffer)
					pipe.dispatch (ctx, 64, 64, 1)
					ctx.wait_idle
					pipe.dispose
				end
			end
	]"
	author: "Larry Rix"
	date: "$Date$"
	revision: "$Revision$"

class
	VULKAN_PIPELINE

create
	make

feature {NONE} -- Initialization

	make (a_ctx: VULKAN_CONTEXT; a_shader: VULKAN_SHADER)
			-- Create compute pipeline from shader.
		require
			ctx_valid: a_ctx /= Void and then a_ctx.is_valid
			shader_valid: a_shader /= Void and then a_shader.is_valid
		do
			context := a_ctx
			shader := a_shader
			handle := svk_create_pipeline (a_ctx.handle, a_shader.handle)
			is_valid := handle /= default_pointer
		ensure
			context_set: context = a_ctx
			shader_set: shader = a_shader
		end

feature -- Access

	handle: POINTER
			-- Opaque handle to svk_pipeline

	context: VULKAN_CONTEXT
			-- Parent context

	shader: VULKAN_SHADER
			-- Associated compute shader

	is_valid: BOOLEAN
			-- Was pipeline creation successful?

feature -- Binding Constants

	Binding_buffer: INTEGER = 0x01
			-- Buffer binding type

	Binding_image: INTEGER = 0x02
			-- Image binding type

	Max_bindings: INTEGER = 8
			-- Maximum bindings per pipeline

feature -- Binding

	bind_buffer (a_binding: INTEGER; a_buffer: VULKAN_BUFFER): BOOLEAN
			-- Bind buffer to pipeline at binding index.
		require
			valid: is_valid
			buffer_valid: a_buffer /= Void and then a_buffer.is_valid
			valid_binding: a_binding >= 0 and a_binding < Max_bindings
		do
			Result := svk_bind_buffer (handle, a_binding.to_natural_32, a_buffer.handle) /= 0
		end

	bind_image (a_binding: INTEGER; a_image: VULKAN_IMAGE): BOOLEAN
			-- Bind image to pipeline at binding index.
		require
			valid: is_valid
			image_valid: a_image /= Void and then a_image.is_valid
			valid_binding: a_binding >= 0 and a_binding < Max_bindings
		do
			Result := svk_bind_image (handle, a_binding.to_natural_32, a_image.handle) /= 0
		end

feature -- Push Constants

	set_push_constants (a_data: POINTER; a_size: INTEGER): BOOLEAN
			-- Set push constant data (small, fast-changing uniforms).
			-- Maximum size is typically 128-256 bytes (GPU-dependent).
		require
			valid: is_valid
			data_attached: a_data /= default_pointer
			valid_size: a_size > 0 and a_size <= 256
		do
			Result := svk_set_push_constants (handle, a_data, a_size.to_natural_32) /= 0
		end

feature -- Dispatch

	dispatch (a_ctx: VULKAN_CONTEXT; a_x, a_y, a_z: INTEGER): BOOLEAN
			-- Dispatch compute shader with specified workgroup counts.
			-- Total threads = workgroup_count * local_size (defined in shader).
		require
			valid: is_valid
			ctx_valid: a_ctx /= Void and then a_ctx.is_valid
			positive_counts: a_x > 0 and a_y > 0 and a_z > 0
		do
			Result := svk_dispatch (a_ctx.handle, handle,
				a_x.to_natural_32, a_y.to_natural_32, a_z.to_natural_32) /= 0
		end

feature -- Synchronization

	wait_idle (a_ctx: VULKAN_CONTEXT)
			-- Wait for GPU to finish all pending operations.
		require
			ctx_valid: a_ctx /= Void and then a_ctx.is_valid
		do
			svk_wait_idle (a_ctx.handle)
		end

feature -- Disposal

	dispose
			-- Free pipeline resources.
		do
			if is_valid and handle /= default_pointer then
				svk_free_pipeline (context.handle, handle)
				handle := default_pointer
				is_valid := False
			end
		ensure
			disposed: not is_valid
			handle_cleared: handle = default_pointer
		end

feature {NONE} -- C Externals

	svk_create_pipeline (ctx, a_shader: POINTER): POINTER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_create_pipeline((svk_context)$ctx, (svk_shader)$a_shader);"
		end

	svk_bind_buffer (pipe: POINTER; binding: NATURAL_32; buf: POINTER): INTEGER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_bind_buffer((svk_pipeline)$pipe, (uint32_t)$binding, (svk_buffer)$buf);"
		end

	svk_bind_image (pipe: POINTER; binding: NATURAL_32; img: POINTER): INTEGER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_bind_image((svk_pipeline)$pipe, (uint32_t)$binding, (svk_image)$img);"
		end

	svk_set_push_constants (pipe, data: POINTER; a_size: NATURAL_32): INTEGER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_set_push_constants((svk_pipeline)$pipe, $data, (uint32_t)$a_size);"
		end

	svk_dispatch (ctx, pipe: POINTER; x, y, z: NATURAL_32): INTEGER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_dispatch((svk_context)$ctx, (svk_pipeline)$pipe, (uint32_t)$x, (uint32_t)$y, (uint32_t)$z);"
		end

	svk_wait_idle (ctx: POINTER)
		external
			"C inline use <simple_vulkan.h>"
		alias
			"svk_wait_idle((svk_context)$ctx);"
		end

	svk_free_pipeline (ctx, pipe: POINTER)
		external
			"C inline use <simple_vulkan.h>"
		alias
			"svk_free_pipeline((svk_context)$ctx, (svk_pipeline)$pipe);"
		end

invariant
	valid_handle: is_valid implies handle /= default_pointer
	context_attached: context /= Void
	shader_attached: shader /= Void

end
