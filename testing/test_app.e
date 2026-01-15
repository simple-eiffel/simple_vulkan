note
	description: "[
		Test application for simple_vulkan library.
		Tests GPU compute capabilities with Vulkan.
	]"
	author: "Larry Rix"
	date: "$Date$"
	revision: "$Revision$"

class
	TEST_APP

create
	make

feature {NONE} -- Initialization

	make
			-- Run tests.
		do
			print ("simple_vulkan Test Application%N")
			print ("===============================%N%N")

			run_tests
		end

feature -- Tests

	run_tests
			-- Execute all tests.
		local
			tests: LIB_TESTS
		do
			create tests.make
			tests.run_all
		end

end
