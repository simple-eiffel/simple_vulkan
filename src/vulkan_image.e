note
	description: "[
		VULKAN_IMAGE - GPU image wrapper for Vulkan compute.

		Manages GPU images suitable for compute shader output.
		Images can be bound to compute pipelines and their
		contents downloaded to CPU memory for display.

		Usage:
			local
				img: VULKAN_IMAGE
				pixels: MANAGED_POINTER
			do
				create img.make (ctx, 1920, 1080, Format_rgba8)
				if img.is_valid then
					-- Bind to pipeline and dispatch
					create pixels.make (img.width * img.height * 4)
					img.download (pixels.item)
					-- Display pixels
					img.dispose
				end
			end
	]"
	author: "Larry Rix"
	date: "$Date$"
	revision: "$Revision$"

class
	VULKAN_IMAGE

create
	make

feature {NONE} -- Initialization

	make (a_ctx: VULKAN_CONTEXT; a_width, a_height: INTEGER; a_format: INTEGER)
			-- Create GPU image for compute shader output.
		require
			ctx_valid: a_ctx /= Void and then a_ctx.is_valid
			positive_dimensions: a_width > 0 and a_height > 0
		do
			context := a_ctx
			width := a_width
			height := a_height
			format := a_format
			handle := svk_create_image (a_ctx.handle,
				a_width.to_natural_32, a_height.to_natural_32, a_format.to_natural_32)
			is_valid := handle /= default_pointer
		ensure
			context_set: context = a_ctx
			width_set: width = a_width
			height_set: height = a_height
			format_set: format = a_format
		end

feature -- Access

	handle: POINTER
			-- Opaque handle to svk_image

	context: VULKAN_CONTEXT
			-- Parent context

	width: INTEGER
			-- Image width in pixels

	height: INTEGER
			-- Image height in pixels

	format: INTEGER
			-- Image format (Format_rgba8, Format_rgba32f)

	is_valid: BOOLEAN
			-- Was image creation successful?

feature -- Format Constants

	Format_rgba8: INTEGER = 0x01
			-- 8-bit RGBA format (4 bytes per pixel)

	Format_rgba32f: INTEGER = 0x02
			-- 32-bit float RGBA format (16 bytes per pixel)

feature -- Computed Properties

	bytes_per_pixel: INTEGER
			-- Bytes per pixel for current format
		do
			inspect format
			when Format_rgba8 then
				Result := 4
			when Format_rgba32f then
				Result := 16
			else
				Result := 4  -- Default to RGBA8
			end
		ensure
			positive: Result > 0
		end

	total_bytes: INTEGER
			-- Total image size in bytes
		do
			Result := width * height * bytes_per_pixel
		ensure
			positive: Result > 0
		end

feature -- Data Transfer

	download (a_data: POINTER): BOOLEAN
			-- Download image data to CPU buffer.
			-- Buffer must be at least `total_bytes` in size.
		require
			valid: is_valid
			data_attached: a_data /= default_pointer
		do
			Result := svk_download_image (context.handle, handle, a_data) /= 0
		end

feature -- Disposal

	dispose
			-- Free GPU image.
		do
			if is_valid and handle /= default_pointer then
				svk_free_image (context.handle, handle)
				handle := default_pointer
				is_valid := False
			end
		ensure
			disposed: not is_valid
			handle_cleared: handle = default_pointer
		end

feature {NONE} -- C Externals

	svk_create_image (ctx: POINTER; a_width, a_height, a_format: NATURAL_32): POINTER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_create_image((svk_context)$ctx, (uint32_t)$a_width, (uint32_t)$a_height, (uint32_t)$a_format);"
		end

	svk_download_image (ctx, img, data: POINTER): INTEGER
		external
			"C inline use <simple_vulkan.h>"
		alias
			"return svk_download_image((svk_context)$ctx, (svk_image)$img, $data);"
		end

	svk_free_image (ctx, img: POINTER)
		external
			"C inline use <simple_vulkan.h>"
		alias
			"svk_free_image((svk_context)$ctx, (svk_image)$img);"
		end

invariant
	valid_handle: is_valid implies handle /= default_pointer
	context_attached: context /= Void
	positive_dimensions: width > 0 and height > 0

end
