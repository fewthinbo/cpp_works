# Function to configure MSVC runtime settings for a target
function(configure_msvc_runtime TARGET_NAME)
    if(MSVC)
        # Set intermediate directory (it's not working)
        set_target_properties(${TARGET_NAME} PROPERTIES 
            VS_INTERMEDIATE_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/Shits/$<CONFIG>/${TARGET_NAME}"
        )
        
        # Set VS-specific options
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:/Od /Zi>
            $<$<CONFIG:Release>:/O2 /Zi>
            $<$<CONFIG:Dist>:/O2>
        )
        
        # Set runtime library using generator expressions since CMAKE_BUILD_TYPE 
        # is not defined for multi-config generators like Visual Studio
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:/MDd>
            $<$<NOT:$<CONFIG:Debug>>:/MD>
        )
    endif()
endfunction()

# Function to configure standard output directories for a target
function(configure_target_output_dirs TARGET_NAME)
    if(WIN32)
        # For Windows/MSVC with multi-config generators
        set_target_properties(${TARGET_NAME} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/Shits/$<CONFIG>/${TARGET_NAME}"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/Libs/$<CONFIG>/${TARGET_NAME}"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/Exec/$<CONFIG>/${TARGET_NAME}"
            PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/Binaries/Shits/$<CONFIG>/${TARGET_NAME}"
            COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/Shits/$<CONFIG>/${TARGET_NAME}"
        )
    else()
        # For single-config generators (FreeBSD, Linux, etc.)
        set_target_properties(${TARGET_NAME} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/Shits/${CMAKE_BUILD_TYPE}/${TARGET_NAME}"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/Libs/${CMAKE_BUILD_TYPE}/${TARGET_NAME}"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/Exec/${CMAKE_BUILD_TYPE}/${TARGET_NAME}"
        )
    endif()
    
    # Only set VS specific properties if we're on Windows with MSVC
    if(MSVC)
        set_target_properties(${TARGET_NAME} PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/Exec/$<CONFIG>/${TARGET_NAME}"
        )
    endif()
endfunction()

# Function to configure an executable target with console subsystem
function(configure_console_app TARGET_NAME)
    if(MSVC)
        set_target_properties(${TARGET_NAME} PROPERTIES 
            LINK_FLAGS "/SUBSYSTEM:CONSOLE"
        )
        
        # Set link-time options
        set_property(TARGET ${TARGET_NAME} PROPERTY 
            VS_GLOBAL_LinkIncremental "$<IF:$<CONFIG:Debug>,true,false>"
        )
    endif()
endfunction()

# Function to ensure there's at least one source file for a target
# Will create a dummy source file with appropriate content if no sources found
function(ensure_sources_exist TARGET_NAME SOURCES_VAR IS_EXECUTABLE)
    # Get the source files list
    set(SOURCES ${${SOURCES_VAR}})
    
    # Check if sources exist
    list(LENGTH SOURCES SOURCE_COUNT)
    if(SOURCE_COUNT EQUAL 0)
        # Create a dummy file path
        set(DUMMY_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_dummy.cpp")
        
        # Write content based on target type
        if(${IS_EXECUTABLE})
            file(WRITE ${DUMMY_SOURCE} "// Auto-generated dummy file for ${TARGET_NAME}\nint main() { return 0; }\n")
        else()
            file(WRITE ${DUMMY_SOURCE} "// Auto-generated dummy file for ${TARGET_NAME}\n")
        endif()
        
        # Set the sources variable in parent scope
        set(${SOURCES_VAR} ${DUMMY_SOURCE} PARENT_SCOPE)
    endif()
endfunction() 