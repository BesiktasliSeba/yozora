function(yozora_add_runtime_clock)
    get_filename_component(yozora_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    add_library(yozora-runtime-wait OBJECT "${yozora_root}/core/runtime/yozora_wait.c")
    set_target_properties(yozora-runtime-wait PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
    target_compile_options(yozora-runtime-wait PRIVATE -Wall -Wextra -Werror)
    target_sources(cxx_static PRIVATE $<TARGET_OBJECTS:yozora-runtime-wait>)
    target_include_directories(cxx_static PRIVATE "${yozora_root}/core/runtime")
    target_compile_definitions(cxx_static PRIVATE YOZORA_PTHREAD_WAIT=1)
    if (APPLE)
        add_library(yozora-darwin OBJECT
            "${yozora_root}/core/darwin/yozora_clock.c"
            "${yozora_root}/core/darwin/yozora_system.c"
            "${yozora_root}/core/darwin/yozora_macho.c"
            "${yozora_root}/core/darwin/yozora_darwin.c")
        set_target_properties(yozora-darwin PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
        target_compile_options(yozora-darwin PRIVATE -Wall -Wextra -Werror)
        target_sources(cxx_static PRIVATE $<TARGET_OBJECTS:yozora-darwin>)
        target_include_directories(cxx_static PRIVATE "${yozora_root}/core/darwin")
    endif ()
endfunction()

cmake_language(DEFER CALL yozora_add_runtime_clock)
