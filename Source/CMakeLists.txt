set(TARGET_NAME Improved)

# Define source files (empty for now, will be populated as you add them)
file(GLOB_RECURSE SOURCES "*.cpp" "*.h" "*.hpp")

# Create the library target
add_library(${TARGET_NAME} STATIC ${SOURCES})

target_include_directories(${TARGET_NAME} PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/Improved
)

configure_common_settings(${TARGET_NAME})