note
	description: "[
		VULKAN_SHADER - Compute shader wrapper for Vulkan.

		Loads and manages SPIR-V compute shaders. Shaders must be
		pre-compiled to SPIR-V format (.spv files) using glslc or
		similar tools.

		Usage:
			local
				shader: VULKAN_SHADER
			do
				create shader.make (ctx, "shaders/compute.spv")
				if shader.is_valid then
					-- Use shader to create pipeline
					shader.dispose
				end
			end
	]"
	author: "Larry Rix"
	date: "$Date$"
	revision: "$Revision$"

class
	VULKAN_SHADER

create
	make,
	make_from_memory

feature {NONE} -- Initialization

	make (a_ctx: VULKAN_CONTEXT; a_spv_path: STRING)
			-- Load compute shader from SPIR-V file.
		require
			ctx_valid: a_ctx /= Void and then a_ctx.is_valid
			path_attached: a_spv_path /= Void and then not a_spv_path.is_empty
		local
			l_c_path: C_STRING
		do
			context := a_ctx
			create l_c_path.make (a_spv_path)
			handle := svk_load_shader (a_ctx.handle, l_c_path.item)
			is_valid := handle /= default_pointer
		ensure
			context_set: context = a_ctx
		end

	make_from_memory (a_ctx: VULKAN_CONTEXT; a_spirv: POINTER; a_size: INTEGER_64)
			-- Load compute shader from SPIR-V memory buffer.
		require
			ctx_valid: a_ctx /= Void and then a_ctx.is_valid
			spirv_attached: a_spirv /= default_pointer
			positive_size: a_size > 0
		do
			context := a_ctx
			handle := svk_load_shader_memory (a_ctx.handle, a_spirv, a_size.to_natural_64)
			is_valid := handle /= default_pointer
		ensure
			context_set: context = a_ctx
		end

feature -- Access

	handle: POINTER
			-- Opaque handle to svk_shader

	context: VULKAN_CONTEXT
			-- Parent context

	is_valid: BOOLEAN
			-- Was shader loading successful?

feature -- Disposal

	dispose
			-- Free shader resources.
		do
			if is_valid and handle /= default_pointer then
				svk_free_shader (context.handle, handle)
				handle := default_pointer
				is_valid := False
			end
		ensure
			disposed: not is_valid
			handle_cleared: handle = default_pointer
		end

feature {NONE} -- C Externals

	svk_load_shader (ctx, path: POINTER): POINTER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_load_shader((svk_context)$ctx, (const char*)$path);"
		end

	svk_load_shader_memory (ctx, spirv: POINTER; a_size: NATURAL_64): POINTER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_load_shader_memory((svk_context)$ctx, (const uint32_t*)$spirv, (uint64_t)$a_size);"
		end

	svk_free_shader (ctx, shader: POINTER)
		external
			"C inline use <simple_vulkan.h>"
		alias
			"svk_free_shader((svk_context)$ctx, (svk_shader)$shader);"
		end

invariant
	valid_handle: is_valid implies handle /= default_pointer
	context_attached: context /= Void

end
