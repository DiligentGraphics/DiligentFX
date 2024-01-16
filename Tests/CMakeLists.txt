cmake_minimum_required (VERSION 3.6)

if(TARGET gtest)
	if(DILIGENT_BUILD_FX_TESTS)

	endif()
endif()

if(DILIGENT_BUILD_FX_INCLUDE_TEST)
	add_subdirectory(IncludeTest)
endif()
