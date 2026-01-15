note
	description: "[
		Test suite for simple_vulkan library.
		Verifies Vulkan initialization, buffer operations,
		and compute shader execution.
	]"
	author: "Larry Rix"
	date: "$Date$"
	revision: "$Revision$"

class
	LIB_TESTS

create
	make

feature {NONE} -- Initialization

	make
			-- Initialize test suite.
		do
			create vk
			passed := 0
			failed := 0
		end

feature -- Access

	vk: SIMPLE_VULKAN
			-- Vulkan facade

	passed: INTEGER
			-- Number of passed tests

	failed: INTEGER
			-- Number of failed tests

feature -- Execution

	run_all
			-- Run all tests.
		do
			test_context_creation
			test_device_info
			test_buffer_creation
			test_buffer_upload_download
			test_image_creation
			test_shader_loading
			test_pipeline_creation

			print ("%N===============================%N")
			print ("Results: " + passed.out + " passed, " + failed.out + " failed%N")
		end

feature -- Individual Tests

	test_context_creation
			-- Test Vulkan context initialization.
		local
			ctx: VULKAN_CONTEXT
		do
			print ("Test: Context creation... ")
			ctx := vk.create_context
			if ctx.is_valid then
				print ("PASS%N")
				passed := passed + 1
				ctx.dispose
			else
				print ("FAIL (Vulkan not available)%N")
				failed := failed + 1
			end
		end

	test_device_info
			-- Test device information queries.
		local
			ctx: VULKAN_CONTEXT
			name: STRING
			vendor: INTEGER
		do
			print ("Test: Device info... ")
			ctx := vk.create_context
			if ctx.is_valid then
				name := ctx.device_name
				vendor := ctx.vendor_id
				print ("PASS%N")
				print ("  Device: " + name + "%N")
				print ("  Vendor: 0x" + vendor.to_hex_string + "%N")
				print ("  Discrete: " + ctx.is_discrete_gpu.out + "%N")
				print ("  Max workgroup: " + ctx.max_workgroup_size.out + "%N")
				passed := passed + 1
				ctx.dispose
			else
				print ("SKIP (no Vulkan)%N")
			end
		end

	test_buffer_creation
			-- Test GPU buffer creation.
		local
			ctx: VULKAN_CONTEXT
			buf: VULKAN_BUFFER
		do
			print ("Test: Buffer creation... ")
			ctx := vk.create_context
			if ctx.is_valid then
				buf := vk.create_buffer (ctx, 1024, vk.Buffer_storage)
				if buf.is_valid then
					print ("PASS%N")
					passed := passed + 1
					buf.dispose
				else
					print ("FAIL (buffer creation failed)%N")
					failed := failed + 1
				end
				ctx.dispose
			else
				print ("SKIP (no Vulkan)%N")
			end
		end

	test_buffer_upload_download
			-- Test buffer data transfer.
		local
			ctx: VULKAN_CONTEXT
			buf: VULKAN_BUFFER
			upload_data: MANAGED_POINTER
			download_data: MANAGED_POINTER
			i: INTEGER
			ok: BOOLEAN
		do
			print ("Test: Buffer upload/download... ")
			ctx := vk.create_context
			if ctx.is_valid then
				buf := vk.create_buffer (ctx, 256, vk.Buffer_storage | vk.Buffer_transfer)
				if buf.is_valid then
					-- Create test data
					create upload_data.make (256)
					create download_data.make (256)

					-- Fill with pattern
					from i := 0 until i >= 256 loop
						upload_data.put_natural_8 ((i \\ 256).to_natural_8, i)
						i := i + 1
					end

					-- Upload
					ok := buf.upload (upload_data.item, 256, 0)
					if ok then
						-- Download
						ok := buf.download (download_data.item, 256, 0)
						if ok then
							-- Verify
							ok := True
							from i := 0 until i >= 256 or not ok loop
								if download_data.read_natural_8 (i) /= (i \\ 256).to_natural_8 then
									ok := False
								end
								i := i + 1
							end
							if ok then
								print ("PASS%N")
								passed := passed + 1
							else
								print ("FAIL (data mismatch)%N")
								failed := failed + 1
							end
						else
							print ("FAIL (download failed)%N")
							failed := failed + 1
						end
					else
						print ("FAIL (upload failed)%N")
						failed := failed + 1
					end
					buf.dispose
				else
					print ("FAIL (buffer creation failed)%N")
					failed := failed + 1
				end
				ctx.dispose
			else
				print ("SKIP (no Vulkan)%N")
			end
		end

	test_image_creation
			-- Test GPU image creation.
		local
			ctx: VULKAN_CONTEXT
			img: VULKAN_IMAGE
		do
			print ("Test: Image creation... ")
			ctx := vk.create_context
			if ctx.is_valid then
				img := vk.create_image (ctx, 1920, 1080, vk.Format_rgba8)
				if img.is_valid then
					print ("PASS%N")
					print ("  Size: " + img.width.out + "x" + img.height.out + "%N")
					print ("  Bytes: " + img.total_bytes.out + "%N")
					passed := passed + 1
					img.dispose
				else
					print ("FAIL (image creation failed)%N")
					failed := failed + 1
				end
				ctx.dispose
			else
				print ("SKIP (no Vulkan)%N")
			end
		end

	test_shader_loading
			-- Test compute shader loading.
		local
			ctx: VULKAN_CONTEXT
			shader: VULKAN_SHADER
			shader_path: STRING
		do
			print ("Test: Shader loading... ")
			ctx := vk.create_context
			if ctx.is_valid then
				-- Try to load the SDF shader
				shader_path := "shaders/sdf_raymarcher.spv"
				shader := vk.load_shader (ctx, shader_path)
				if shader.is_valid then
					print ("PASS%N")
					passed := passed + 1
					shader.dispose
				else
					print ("SKIP (shader not compiled)%N")
					print ("  Run: glslc shaders/sdf_raymarcher.comp -o shaders/sdf_raymarcher.spv%N")
				end
				ctx.dispose
			else
				print ("SKIP (no Vulkan)%N")
			end
		end

	test_pipeline_creation
			-- Test compute pipeline creation.
		local
			ctx: VULKAN_CONTEXT
			shader: VULKAN_SHADER
			pipeline: VULKAN_PIPELINE
			shader_path: STRING
		do
			print ("Test: Pipeline creation... ")
			ctx := vk.create_context
			if ctx.is_valid then
				shader_path := "shaders/sdf_raymarcher.spv"
				shader := vk.load_shader (ctx, shader_path)
				if shader.is_valid then
					pipeline := vk.create_pipeline (ctx, shader)
					if pipeline.is_valid then
						print ("PASS%N")
						passed := passed + 1
						pipeline.dispose
					else
						print ("FAIL (pipeline creation failed)%N")
						failed := failed + 1
					end
					shader.dispose
				else
					print ("SKIP (shader not compiled)%N")
				end
				ctx.dispose
			else
				print ("SKIP (no Vulkan)%N")
			end
		end

end
