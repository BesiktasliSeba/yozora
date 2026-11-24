include("${CMAKE_CURRENT_LIST_DIR}/Probe.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/WebCoreProbe.cmake")

function(yozora_add_ios_page)
    get_filename_component(yozora_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    target_link_libraries(Skia PUBLIC "-framework CoreFoundation")
    # upstream adds an objc++ prefix stub to every apple clang target and the ios 5 sdk cannot build wtf as
    # objc++ (no CFAutorelease, no String(NSString *) without USE(FOUNDATION)), these targets have no objc++
    # sources
    foreach (yozora_target IN ITEMS WebCore WebCoreDOMAndRendering PAL)
        get_target_property(yozora_sources ${yozora_target} SOURCES)
        list(FILTER yozora_sources EXCLUDE REGEX "_pch_obj\\.mm$")
        set_property(TARGET ${yozora_target} PROPERTY SOURCES "${yozora_sources}")
    endforeach ()
    # prefix-header subtargets must be inside the distributable static archive
    get_target_property(yozora_links WebCore INTERFACE_LINK_LIBRARIES)
    foreach (yozora_link IN LISTS yozora_links)
        if (yozora_link MATCHES "TARGET_OBJECTS:([^>]+)>")
            target_sources(WebCore PRIVATE "${yozora_link}")
        endif ()
    endforeach ()
    list(FILTER yozora_links EXCLUDE REGEX "TARGET_OBJECTS:")
    set_property(TARGET WebCore PROPERTY INTERFACE_LINK_LIBRARIES "${yozora_links}")
    set(yozora_test_api "${CMAKE_SOURCE_DIR}/Tools/TestWebKitAPI/Tests/WebCore")
    configure_file("${WEBCORE_DIR}/testing/TestPage.h"
        "${CMAKE_BINARY_DIR}/yozora-test-headers/WebCoreTestSupport/TestPage.h" COPYONLY)
    add_executable(yozora-page
        "${yozora_root}/tests/test_webcore_page.cpp"
        "${yozora_root}/tests/test_webcore_services.cpp"
        "${yozora_root}/tests/yozora_test_page.cpp"
        "${yozora_test_api}/TestPlatformStrategies.cpp"
        "${yozora_root}/core/yozora_log.c")
    get_target_property(yozora_includes WebCore INCLUDE_DIRECTORIES)
    target_include_directories(yozora-page PRIVATE ${yozora_includes}
        "${CMAKE_BINARY_DIR}" "${JavaScriptCore_FRAMEWORK_HEADERS_DIR}"
        "${JavaScriptCore_PRIVATE_FRAMEWORK_HEADERS_DIR}" "${WebCore_PRIVATE_FRAMEWORK_HEADERS_DIR}"
        "${Skia_FRAMEWORK_HEADERS_DIR}/top" "${Skia_FRAMEWORK_HEADERS_DIR}/mod"
        "${YOZORA_WEBCORE_ROOT}" "${yozora_test_api}" "${CMAKE_BINARY_DIR}/yozora-test-headers")
    set(yozora-page_FRAMEWORKS WebCore JavaScriptCore PAL WTF bmalloc)
    _WEBKIT_TARGET_LINK_FRAMEWORK(yozora-page)
    target_link_libraries(yozora-page PRIVATE ${yozora-page_PRIVATE_LIBRARIES})
    if (ENABLE_WEB_AUDIO)
        # AudioDecodeDarwin.cpp reads files with ExtAudioFile and AudioOutputDarwin.cpp plays through an AudioQueue, both in WebCore
        target_link_libraries(yozora-page PRIVATE "-framework AudioToolbox")
    endif ()
    target_link_options(yozora-page PRIVATE "LINKER:-dead_strip")
endfunction()

cmake_language(DEFER CALL yozora_add_ios_page)

# code that pages seldom run is built for size, the engine keeps -O3 where a page spends its time. the unified bundles are named after their
# directory (UnifiedSource-inspector-3.cpp), so a directory is cold or hot as a whole and a bundle never mixes the two. the flag comes after -O3
# in the command line and wins. clang refuses a precompiled header made at another optimization level, so these bundles skip the prefix header
function(yozora_size_optimize_cold_bundles)
    # the bundles of webcore sit in several targets, it is split by directory. javascriptcore keeps -O3: its patched jit headers lean on the prefix
    # header (InlineCacheCompiler.h names CCallHelpers without including it), which a bundle without it does not have
    set(yozora_cold "Modules|inspector|accessibility|automation|plugins|history|bridge|crypto|contentextensions|fileapi|worklets|xml|mathml")
    set(yozora_cold_total 0)
    foreach (yozora_target IN ITEMS WebCore WebCoreDOMAndRendering WebCoreStyle WebCoreInspector WebCoreJSBindings)
        if (NOT TARGET ${yozora_target})
            continue ()
        endif ()
        get_target_property(yozora_sources ${yozora_target} SOURCES)
        list(FILTER yozora_sources INCLUDE REGEX "unified-sources/UnifiedSource-(${yozora_cold})-[0-9]+\\.cpp$")
        list(LENGTH yozora_sources yozora_count)
        if (yozora_count)
            set_source_files_properties(${yozora_sources} TARGET_DIRECTORY ${yozora_target} PROPERTIES COMPILE_OPTIONS "-Os" SKIP_PRECOMPILE_HEADERS ON)
            math(EXPR yozora_cold_total "${yozora_cold_total} + ${yozora_count}")
        endif ()
    endforeach ()
    message(STATUS "yozora: ${yozora_cold_total} cold bundles are built for size")
endfunction()

cmake_language(DEFER CALL yozora_size_optimize_cold_bundles)
