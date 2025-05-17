# Function to add Thread Sanitizer support to a target (only on Unix-like systems)
function(add_thread_sanitizer_to_target TARGET_NAME)
    if(USE_THREAD_SANITIZER AND NOT PLATFORM_WINDOWS)
        # Set global flags first (before target-specific ones)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -fno-omit-frame-pointer" PARENT_SCOPE)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread" PARENT_SCOPE)

        # Create log directory with proper permissions
        set(TSAN_LOG_DIR "${CMAKE_BINARY_DIR}/logs/tsan")
        file(MAKE_DIRECTORY ${TSAN_LOG_DIR})
        file(CHMOD ${TSAN_LOG_DIR} PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

        # TSan runtime options
        target_compile_options(${TARGET_NAME} PRIVATE 
            -fsanitize=thread
            -g
            -fno-omit-frame-pointer
        )
        
        # Link flags need to be separate too
        target_link_options(${TARGET_NAME} PRIVATE 
            -fsanitize=thread
        )
        
        # Add runtime library dependencies
        if(NOT PLATFORM_MACOS)
            target_link_libraries(${TARGET_NAME} PRIVATE pthread dl)
        endif()
        
        if(PLATFORM_FREEBSD)
            find_program(LLVM_SYMBOLIZER llvm-symbolizer PATHS /usr/local/bin)
            if(LLVM_SYMBOLIZER)
                target_compile_options(${TARGET_NAME} PRIVATE 
                    "-DTSAN_SYMBOLIZER_PATH=${LLVM_SYMBOLIZER}"
                )
                message(STATUS "Using llvm-symbolizer at ${LLVM_SYMBOLIZER}")
            endif()
        endif()

        # Set TSan runtime options as compile definitions
        target_compile_definitions(${TARGET_NAME} PRIVATE 
            THREAD_SANITIZER
            "TSAN_OPTIONS=log_path=${TSAN_LOG_DIR}/tsan.log:history_size=7:verbosity=1:report_thread_leaks=0:report_signal_unsafe=1:report_destroy_locked=1:second_deadlock_stack=1:detect_deadlocks=1:halt_on_error=0:flush_memory_ms=100"
        )

        message(STATUS "Thread Sanitizer enabled for target ${TARGET_NAME}")
        message(STATUS "TSan logs will be saved to: ${TSAN_LOG_DIR}/tsan.log")
        message(STATUS "TSan runtime options set through compile definitions")
    endif()
endfunction()

function(add_asan_to_target TARGET_NAME)
    if(USE_ASAN AND NOT PLATFORM_WINDOWS)
        # Set global flags first (before target-specific ones)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer" PARENT_SCOPE)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address" PARENT_SCOPE)
        
        # Create log directory with proper permissions
        set(ASAN_LOG_DIR "${CMAKE_BINARY_DIR}/logs/asan")
        file(MAKE_DIRECTORY ${ASAN_LOG_DIR})
        file(CHMOD ${ASAN_LOG_DIR} PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

        # ASan runtime options
        target_compile_options(${TARGET_NAME} PRIVATE 
            -fsanitize=address
            -g
            -fno-omit-frame-pointer
        )
        
        target_link_options(${TARGET_NAME} PRIVATE 
            -fsanitize=address
        )
        
        # Add runtime library dependencies
        if(NOT PLATFORM_MACOS)
            target_link_libraries(${TARGET_NAME} PRIVATE pthread dl)
        endif()
        
        if(PLATFORM_FREEBSD)
            find_program(LLVM_SYMBOLIZER llvm-symbolizer PATHS /usr/local/bin)
            if(LLVM_SYMBOLIZER)
                target_compile_options(${TARGET_NAME} PRIVATE 
                    "-DASAN_SYMBOLIZER_PATH=${LLVM_SYMBOLIZER}"
                )
                message(STATUS "Using llvm-symbolizer at ${LLVM_SYMBOLIZER}")
            endif()
        endif()

        # Set ASan runtime options as compile definitions
        target_compile_definitions(${TARGET_NAME} PRIVATE 
            ASAN_ENABLED
            "ASAN_OPTIONS=log_path=${ASAN_LOG_DIR}/asan.log:check_initialization_order=1:strict_init_order=1"
        )

        message(STATUS "Address Sanitizer enabled for target ${TARGET_NAME}")
        message(STATUS "ASan logs will be saved to: ${ASAN_LOG_DIR}/asan.log")
        message(STATUS "ASan runtime options set through compile definitions")
    endif()
endfunction()

# Function to add Helgrind support to a target (only on Unix-like systems)
function(add_helgrind_to_target TARGET_NAME)
    if(USE_HELGRIND AND NOT PLATFORM_WINDOWS)
        # Helgrind requires debug symbols
        target_compile_options(${TARGET_NAME} PRIVATE -g)
        target_compile_definitions(${TARGET_NAME} PRIVATE HELGRIND_ENABLED)
        
        # Ensure PThread is linked (Helgrind needs to track PThread operations)
        if(NOT PLATFORM_MACOS)
            # pthread options on Linux/FreeBSD
            target_link_libraries(${TARGET_NAME} PRIVATE pthread)
        endif()
        
        # Helgrind için komut göster
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Helgrind is enabled for ${TARGET_NAME}"
            COMMAND ${CMAKE_COMMAND} -E echo "To run with Helgrind, use: valgrind --tool=helgrind ${CMAKE_BINARY_DIR}/Binaries/Exec/${CMAKE_BUILD_TYPE}/${TARGET_NAME}"
        )
        
        message(STATUS "Helgrind support enabled for target ${TARGET_NAME}")
    endif()
endfunction()

# Function to add Clang-Tidy to a target
function(add_clang_tidy_to_target TARGET_NAME)
    if(USE_CLANG_TIDY AND CLANG_TIDY_EXE)
        set_target_properties(${TARGET_NAME} PROPERTIES 
            CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-checks=*,-fuchsia-*,-google-*,-llvm-*,-android-*,-readability-magic-numbers,-cppcoreguidelines-avoid-magic-numbers"
        )
        message(STATUS "Clang-Tidy enabled for target ${TARGET_NAME}")
    endif()
endfunction()

# Sanitizer ve statik analiz araçlarını hedeflere eklemek için ana fonksiyon
function(apply_code_quality_tools TARGET_NAME)
    # ASan ve TSan birlikte kullanılamazlar, kontrol et
    if(USE_THREAD_SANITIZER AND USE_ASAN)
        message(WARNING "Address Sanitizer ve Thread Sanitizer aynı anda kullanılamaz! Sadece birini seçin.")
        # TSan'ı devre dışı bırak, ASan'ı kullan
        set(USE_THREAD_SANITIZER OFF CACHE BOOL "Use Thread Sanitizer" FORCE)
        message(STATUS "Thread Sanitizer devre dışı bırakıldı, Address Sanitizer kullanılacak.")
    endif()

    # Önce ASan'ı ekle (eğer aktifse)
    add_asan_to_target(${TARGET_NAME})
    
    # Sonra diğer araçları ekle
    add_thread_sanitizer_to_target(${TARGET_NAME})
    add_clang_tidy_to_target(${TARGET_NAME})
    add_helgrind_to_target(${TARGET_NAME})

    # Test hedefleri için CTest yapılandırması
    if(${TARGET_NAME} STREQUAL "UnitTests")
        # ASan için CTest girdisi ekle
        if(USE_ASAN AND NOT PLATFORM_WINDOWS)
            add_test(
                NAME ${TARGET_NAME}_asan
                COMMAND ${CMAKE_BINARY_DIR}/Binaries/Exec/${CMAKE_BUILD_TYPE}/${TARGET_NAME}
            )
        endif()
        
        # TSan için CTest girdisi ekle
        if(USE_THREAD_SANITIZER AND NOT PLATFORM_WINDOWS)
            add_test(
                NAME ${TARGET_NAME}_tsan
                COMMAND ${CMAKE_BINARY_DIR}/Binaries/Exec/${CMAKE_BUILD_TYPE}/${TARGET_NAME}
            )
        endif()
        
        # Helgrind için CTest girdisi ekle
        if(USE_HELGRIND AND NOT PLATFORM_WINDOWS AND VALGRIND_EXECUTABLE)
            set(HELGRIND_LOG_DIR "${CMAKE_BINARY_DIR}/codeQuality/helgrind_logs")
            file(MAKE_DIRECTORY ${HELGRIND_LOG_DIR})
            
            add_test(
                NAME ${TARGET_NAME}_helgrind
                COMMAND ${VALGRIND_EXECUTABLE}
                    --tool=helgrind
                    --quiet
                    --trace-children=yes
                    --log-file=${HELGRIND_LOG_DIR}/helgrind_log_%p.txt
                    ${CMAKE_BINARY_DIR}/Binaries/Exec/${CMAKE_BUILD_TYPE}/${TARGET_NAME}
            )
        endif()
    endif()
endfunction()

