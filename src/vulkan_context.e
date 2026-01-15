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
			"C inline use <simple_vulkan.h>"
		alias
			"svk_cleanup((svk_context)$ctx);"
		end

invariant
	valid_handle: is_valid implies handle /= default_pointer

end
