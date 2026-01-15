note
	description: "[
		SIMPLE_VULKAN - Facade class for Vulkan GPU compute.

		Provides cross-platform GPU acceleration for NVIDIA, AMD, and Intel GPUs.
		Designed for high-performance parallel computation, particularly
		SDF ray marching for real-time visualization.

		Usage:
			local
				vk: SIMPLE_VULKAN
				ctx: VULKAN_CONTEXT
				buf: VULKAN_BUFFER
			do
				create vk
				ctx := vk.create_context
				if ctx.is_valid then
					print (ctx.device_name)
					buf := vk.create_buffer (ctx, 1024, vk.Buffer_storage)
					-- ... use buffer ...
					buf.dispose
				end
				ctx.dispose
			end
	]"
	author: "Larry Rix"
	date: "$Date$"
	revision: "$Revision$"

class
	SIMPLE_VULKAN

feature -- Context Factory

	create_context: VULKAN_CONTEXT
			-- Create and initialize Vulkan context.
			-- Automatically selects best available GPU.
		do
			create Result.make
		ensure
			result_attached: Result /= Void
		end

feature -- Buffer Factory

	create_buffer (a_ctx: VULKAN_CONTEXT; a_size: INTEGER_64; a_usage: INTEGER): VULKAN_BUFFER
			-- Create GPU buffer with specified size and usage flags.
		require
			ctx_valid: a_ctx /= Void and then a_ctx.is_valid
			positive_size: a_size > 0
		do
			create Result.make (a_ctx, a_size, a_usage)
		ensure
			result_attached: Result /= Void
		end

feature -- Shader Factory

	load_shader (a_ctx: VULKAN_CONTEXT; a_spv_path: STRING): VULKAN_SHADER
			-- Load compute shader from SPIR-V file.
		require
			ctx_valid: a_ctx /= Void and then a_ctx.is_valid
			path_attached: a_spv_path /= Void
		do
			create Result.make (a_ctx, a_spv_path)
		ensure
			result_attached: Result /= Void
		end

feature -- Pipeline Factory

	create_pipeline (a_ctx: VULKAN_CONTEXT; a_shader: VULKAN_SHADER): VULKAN_PIPELINE
			-- Create compute pipeline from shader.
		require
			ctx_valid: a_ctx /= Void and then a_ctx.is_valid
			shader_valid: a_shader /= Void and then a_shader.is_valid
		do
			create Result.make (a_ctx, a_shader)
		ensure
			result_attached: Result /= Void
		end

feature -- Image Factory

	create_image (a_ctx: VULKAN_CONTEXT; a_width, a_height: INTEGER; a_format: INTEGER): VULKAN_IMAGE
			-- Create GPU image for compute shader output.
		require
			ctx_valid: a_ctx /= Void and then a_ctx.is_valid
			positive_dimensions: a_width > 0 and a_height > 0
		do
			create Result.make (a_ctx, a_width, a_height, a_format)
		ensure
			result_attached: Result /= Void
		end

feature -- Buffer Usage Flags

	Buffer_storage: INTEGER = 0x01
			-- Shader storage buffer (SSBO)

	Buffer_uniform: INTEGER = 0x02
			-- Uniform buffer

	Buffer_transfer: INTEGER = 0x04
			-- Transfer source/destination

feature -- Image Format

	Format_rgba8: INTEGER = 0x01
			-- 8-bit RGBA format

	Format_rgba32f: INTEGER = 0x02
			-- 32-bit float RGBA format

feature -- Vendor IDs

	Vendor_nvidia: INTEGER = 0x10DE
			-- NVIDIA vendor ID

	Vendor_amd: INTEGER = 0x1002
			-- AMD vendor ID

	Vendor_intel: INTEGER = 0x8086
			-- Intel vendor ID

end
