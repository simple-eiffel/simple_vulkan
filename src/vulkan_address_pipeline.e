note
	description: "[
		VULKAN_ADDRESS_PIPELINE - a compute pipeline with no descriptor sets: the
		shader reads its buffers through device addresses (GL_EXT_buffer_reference)
		handed over in up to `Max_push_bytes' of push constants, so one pipeline can
		be recorded many times with different buffers in a single batch.

		`record' adds a dispatch (and a full memory barrier after it) to the
		context's open batch; the GPU runs it at the next `flush', `read' or `write'.

		Shader shape:
			#extension GL_EXT_buffer_reference : require
			layout(buffer_reference, std430) buffer Floats { float v[]; };
			layout(push_constant) uniform P { Floats a; Floats out_; uint n; } p;
	]"
	author: "Larry Rix"

class
	VULKAN_ADDRESS_PIPELINE

create
	make

feature {NONE} -- Initialization

	make (a_ctx: VULKAN_CONTEXT; a_shader: VULKAN_SHADER)
			-- Pipeline running `a_shader' (entry point "main").
		require
			ctx_valid: a_ctx.is_valid
			addressable: a_ctx.has_device_address
			shader_valid: a_shader.is_valid
		do
			context := a_ctx
			handle := svk_create_address_pipeline (a_ctx.handle, a_shader.handle)
			is_valid := handle /= default_pointer
		ensure
			context_set: context = a_ctx
		end

feature -- Access

	handle: POINTER
	context: VULKAN_CONTEXT
	is_valid: BOOLEAN

	Max_push_bytes: INTEGER = 128
			-- Push-constant bytes available (Vulkan's guaranteed minimum).

feature -- Recording

	record (a_push: POINTER; a_push_size: INTEGER; a_groups_x, a_groups_y, a_groups_z: INTEGER): BOOLEAN
			-- Record a dispatch of `a_groups_x' x `a_groups_y' x `a_groups_z' workgroups
			-- with push constants `a_push' (`a_push_size' bytes).
		require
			valid: is_valid
			push_fits: 0 <= a_push_size and a_push_size <= Max_push_bytes
			push_attached: a_push_size > 0 implies a_push /= default_pointer
			groups_positive: a_groups_x >= 1 and a_groups_y >= 1 and a_groups_z >= 1
		do
			Result := svk_record_dispatch (context.handle, handle, a_push, a_push_size.to_natural_32,
				a_groups_x.to_natural_32, a_groups_y.to_natural_32, a_groups_z.to_natural_32) /= 0
		end

feature -- Profiling (0.4.0)

	profiled_ms: REAL_64
			-- GPU time of this pipeline's dispatches recorded while profiling was on.
		require
			valid: is_valid
		do
			Result := svk_pipeline_profiled_ns (handle).to_real_64 / 1.0e6
		end

	profiled_calls: INTEGER_64
			-- Dispatches timed.
		require
			valid: is_valid
		do
			Result := svk_pipeline_profiled_calls (handle).to_integer_64
		end

	reset_profile
		require
			valid: is_valid
		do
			svk_pipeline_reset_profile (handle)
		ensure
			cleared: profiled_calls = 0
		end

feature -- Disposal

	dispose
			-- Free the pipeline (runs recorded work first).
		do
			if is_valid then
				svk_free_pipeline (context.handle, handle)
				handle := default_pointer
				is_valid := False
			end
		ensure
			disposed: not is_valid and handle = default_pointer
		end

feature {NONE} -- C externals

	svk_create_address_pipeline (ctx, shader: POINTER): POINTER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_create_address_pipeline((svk_context)$ctx, (svk_shader)$shader);"
		end

	svk_record_dispatch (ctx, pipe, push: POINTER; a_push_size, x, y, z: NATURAL_32): INTEGER
		external
			"C blocking inline use <simple_vulkan.h>"
		alias
			"return svk_record_dispatch((svk_context)$ctx, (svk_pipeline)$pipe, $push, (uint32_t)$a_push_size, (uint32_t)$x, (uint32_t)$y, (uint32_t)$z);"
		end

	svk_pipeline_profiled_ns (pipe: POINTER): NATURAL_64
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return (EIF_NATURAL_64) svk_pipeline_profiled_ns((svk_pipeline)$pipe);"
		end

	svk_pipeline_profiled_calls (pipe: POINTER): NATURAL_64
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return (EIF_NATURAL_64) svk_pipeline_profiled_calls((svk_pipeline)$pipe);"
		end

	svk_pipeline_reset_profile (pipe: POINTER)
		external
			"C inline use <simple_vulkan.h>"
		alias
			"svk_pipeline_reset_profile((svk_pipeline)$pipe);"
		end

	svk_free_pipeline (ctx, pipe: POINTER)
		external
			"C blocking inline use <simple_vulkan.h>"
		alias
			"svk_free_pipeline((svk_context)$ctx, (svk_pipeline)$pipe);"
		end

invariant
	valid_handle: is_valid implies handle /= default_pointer

end
