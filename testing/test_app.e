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
			batch: BATCH_TESTS
		do
			create tests.make
			tests.run_all
			create batch.make
			batch.run_all
			print ("Batch results: " + batch.passed.out + " passed, " + batch.failed.out + " failed%N")
			if tests.failed + batch.failed > 0 then
					-- port-coordination #17: a batch failure printed only "FAIL" and the program
					-- exited 0, so nothing reading it saw a failure. Say it, and exit 1.
				print ("TESTS FAILED%N")
				(create {EXCEPTIONS}).die (1)
			end
		end

end
