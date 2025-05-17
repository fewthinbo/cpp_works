cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to be used")
set(CMAKE_CXX_STANDARD_REQUIRED ON CACHE BOOL "Require C++ standard")
set(CMAKE_CXX_EXTENSIONS OFF CACHE BOOL "Disable C++ extensions")

# Önce cmake modül yolunu ayarla
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# vcpkg.json kullanýyoruz, manifest modu aktif olmalý
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
		CACHE STRING "Vcpkg toolchain file")
	message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
elseif(EXISTS "E:\\git.zyf\\vcpkg\\scripts\\buildsystems\\vcpkg.cmake" AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	set(CMAKE_TOOLCHAIN_FILE "E:\\git.zyf\\vcpkg\\scripts\\buildsystems\\vcpkg.cmake"
		CACHE STRING "Vcpkg toolchain file")
	message(STATUS "Using vcpkg toolchain from hardcoded path: ${CMAKE_TOOLCHAIN_FILE}")
elseif(PLATFORM_FREEBSD AND EXISTS "$ENV{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake" AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake"
		CACHE STRING "Vcpkg toolchain file on FreeBSD")
	message(STATUS "Using vcpkg toolchain on FreeBSD: ${CMAKE_TOOLCHAIN_FILE}")
endif()

# Derleyici seçimi (platform bazlý)
if(WIN32)
	# Windows için Clang-CL kullanmak istiyorsanýz (MSVC uyumlu Clang)
	# Not: Visual Studio içinde "Clang for Windows" yüklü olmalý
	option(USE_CLANG_ON_WINDOWS "Use Clang compiler on Windows" ON)
	
	if(USE_CLANG_ON_WINDOWS)
		# Derleyici ayarlarýný CACHE ile zorla
		set(CMAKE_C_COMPILER "clang-cl" CACHE STRING "C compiler" FORCE)
		set(CMAKE_CXX_COMPILER "clang-cl" CACHE STRING "C++ compiler" FORCE)

		# MSVC uyumluluðu için gerekli bayraklar
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc" CACHE STRING "C++ flags" FORCE)

		message(STATUS "Using Clang compiler on Windows")
	endif()
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
	# FreeBSD için Clang
	set(CMAKE_C_COMPILER "clang")
	set(CMAKE_CXX_COMPILER "clang++")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
	message(STATUS "Using Clang compiler on FreeBSD")
endif()

project(CppWorks VERSION 1.0.0 LANGUAGES CXX)

# Diðer modülleri ekle
include(MSVCRuntime)
include(CodeQuality)

# Detect platform
if(WIN32)
	set(PLATFORM_WINDOWS TRUE)
	message("OS name is windows") 
elseif(APPLE)
	set(PLATFORM_MACOS TRUE)
	message("OS name is macos") 
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
	set(PLATFORM_FREEBSD TRUE)
	message("OS name is freebsd") 
elseif(UNIX)
	set(PLATFORM_LINUX TRUE)
	message("OS name is linux") 
endif()

# Options for enabling/disabling features
option(USE_THREAD_SANITIZER "Enable Thread Sanitizer (Supported on Unix-like systems only)" OFF)
option(USE_CLANG_TIDY "Enable Clang-Tidy code analysis" OFF)
option(USE_ASAN "Enable Adress Sanitizer" OFF)
option(USE_HELGRIND "Enable Valgrind Helgrind detector (Supported on Unix-like systems only)" OFF)
option(BUILD_TESTS "Build tests using GoogleTest" OFF)

# Display available options
message(STATUS "Build options:")
message(STATUS "  - BUILD_TESTS: ${BUILD_TESTS}")
message(STATUS "  - USE_THREAD_SANITIZER: ${USE_THREAD_SANITIZER}")
message(STATUS "  - USE_HELGRIND: ${USE_HELGRIND}")
message(STATUS "  - USE_CLANG_TIDY: ${USE_CLANG_TIDY}")
message(STATUS "  - USE_ASAN : ${USE_ASAN}")


# Define custom configurations
# In addition to standard Debug and Release, add Dist configuration
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Dist" CACHE STRING "" FORCE)

# GoogleTest Configuration
if(BUILD_TESTS)
	enable_testing()
	# Find threads package for both platforms - needed by GoogleTest
	find_package(Threads REQUIRED)
	include(FetchContent)
	FetchContent_Declare(
		googletest
		GIT_REPOSITORY https://github.com/google/googletest.git
		GIT_TAG release-1.12.1
	)
	
	# For Windows: Prevent overriding the parent project's compiler/linker settings
	set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
	
	# Disable GoogleTest CMake warnings
	set(CMAKE_POLICY_DEFAULT_CMP0048 NEW)
	
	# Clang için özel GoogleTest ayarlarý
	if(USE_CLANG_ON_WINDOWS)
		set(GTEST_HAS_TR1_TUPLE 0 CACHE BOOL "" FORCE)
		set(GTEST_USE_OWN_TR1_TUPLE 0 CACHE BOOL "" FORCE)
		set(GTEST_HAS_PTHREAD 0 CACHE BOOL "" FORCE)
		set(GTEST_HAS_RTTI 1 CACHE BOOL "" FORCE)
		set(GTEST_HAS_EXCEPTIONS 1 CACHE BOOL "" FORCE)
		set(GTEST_HAS_STD_WSTRING 1 CACHE BOOL "" FORCE)
		set(GTEST_HAS_SEH 1 CACHE BOOL "" FORCE)
		set(GTEST_HAS_STREAM_REDIRECTION 1 CACHE BOOL "" FORCE)
		set(GTEST_HAS_DEATH_TEST 1 CACHE BOOL "" FORCE)
		set(GTEST_HAS_TYPED_TEST 1 CACHE BOOL "" FORCE)
		set(GTEST_HAS_TYPED_TEST_P 1 CACHE BOOL "" FORCE)
		set(GTEST_HAS_PARAM_TEST 1 CACHE BOOL "" FORCE)
		set(GTEST_HAS_PROTOBUF 0 CACHE BOOL "" FORCE)
		set(GTEST_HAS_ABSL 0 CACHE BOOL "" FORCE)
	endif()
	
	# Make GoogleTest available
	FetchContent_MakeAvailable(googletest)
	
	# Helpers for testing
	include(GoogleTest)
endif()

# Output directories configuration
if(PLATFORM_WINDOWS)
	# For Windows, use generator expressions for multi-config generators
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Binaries/Shits/$<CONFIG>)
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Binaries/Libs/$<CONFIG>)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Binaries/Exec/$<CONFIG>)
else()
	# For single-config generators like Ninja on FreeBSD
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Binaries/Shits/${CMAKE_BUILD_TYPE})
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Binaries/Libs/${CMAKE_BUILD_TYPE})
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Binaries/Exec/${CMAKE_BUILD_TYPE})
endif()

set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps" CACHE PATH "FetchContent cache directory") # For FetchContent

# Platform-specific compiler settings
if(PLATFORM_WINDOWS)    
	# Set properties for all targets to use consistent path structures
	# This ensures Visual Studio projects use the same output paths
	if(MSVC)
		set_property(GLOBAL PROPERTY USE_FOLDERS ON)
		
		# Keep source groups matching to disk
		set_property(GLOBAL PROPERTY USE_SOURCE_GROUPS ON)
		
		# Generic paths for Visual Studio
		set(CMAKE_VS_INCLUDE_INSTALL_TO_DEFAULT_BUILD ON)
		
		# Use folders in Solution Explorer
		set_property(GLOBAL PROPERTY USE_FOLDERS ON)
		
		# Set MSVC runtime library
		set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
		
		# Keep paths generic and consistent
		set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT "App_Main")
		
		# Set default properties for Visual Studio
		add_compile_options(
			"$<$<CONFIG:Debug>:/Od>"
			"$<$<CONFIG:Debug>:/Zi>"
			"$<$<NOT:$<CONFIG:Debug>>:/O2>"
			/MP  # Enable multi-processor compilation
			/EHsc  # Enable C++ exception handling
			/Zc:__cplusplus  # Make __cplusplus macro accurate
			/Zc:preprocessor  # Use new preprocessor
		)
		
		# Define common preprocessor definitions from VCXPROJ
		add_compile_definitions(
			"$<$<CONFIG:Debug>:DEBUG>"
			"$<$<CONFIG:Release>:NDEBUG;RELEASE>"
			"$<$<CONFIG:Dist>:NDEBUG;DIST>"
			NOMINMAX
			_WIN32_WINNT=0x0601
			WIN32_LEAN_AND_MEAN
			PLATFORM_WINDOWS
		)
	endif()
elseif(PLATFORM_FREEBSD)
	# FreeBSD specific settings
	# Add optimization flags based on build type
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		add_compile_options(-O0 -g)
	elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
		add_compile_options(-O2)
	elseif(CMAKE_BUILD_TYPE STREQUAL "Dist")
		add_compile_options(-O3)
	endif()
	
	# Common compiler warnings and flags for FreeBSD
	add_compile_options(-Wall -Wextra)
	
	# Define FreeBSD platform using direct definitions instead of generator expressions
	add_compile_definitions(PLATFORM_FREEBSD)
	
	# Add configuration-specific definitions without generator expressions
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		add_compile_definitions(DEBUG)
	elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
		add_compile_definitions(NDEBUG RELEASE)
	elseif(CMAKE_BUILD_TYPE STREQUAL "Dist")
		add_compile_definitions(NDEBUG DIST)
	endif()
endif()

function(configure_common_settings target_name)
	# Set default C++ standard for all targets

	# Configure output directories
	configure_target_output_dirs(${target_name})
	
	# Configure MSVC runtime
	configure_msvc_runtime(${target_name})
	
	# Set warning levels based on platform
	if(PLATFORM_WINDOWS)
		if(MSVC)
			target_compile_options(${target_name} PRIVATE /W4)
		else() # Clang on Windows
			target_compile_options(${target_name} PRIVATE -Wall)
		endif()
	else() # Unix-like systems
		target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
	endif()
endfunction()

# Make sure source directories exist
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/Source)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/Extern/include)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/Extern/lib)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/Tests)

# Add subdirectories for each component
add_subdirectory(Source)

# Add tests directory if tests are enabled
if(BUILD_TESTS)
	add_subdirectory(Tests)
endif()

