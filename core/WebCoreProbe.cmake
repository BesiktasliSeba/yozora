function(yozora_add_graphics_probe)
    get_filename_component(yozora_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    add_executable(yozora-skia "${yozora_root}/tools/yozora_skia_probe.cpp")
    target_include_directories(yozora-skia SYSTEM PRIVATE
        "${Skia_FRAMEWORK_HEADERS_DIR}/top" "${Skia_FRAMEWORK_HEADERS_DIR}/mod")
    target_link_libraries(yozora-skia PRIVATE Skia::Skia)
    # link every raster object so unused platform imports cannot hide in the archive
    if (APPLE)
        target_link_options(yozora-skia PRIVATE "LINKER:-force_load,$<TARGET_FILE:Skia>")
    else ()
        target_link_options(yozora-skia PRIVATE "LINKER:--no-gc-sections,--whole-archive,$<TARGET_FILE:Skia>,--no-whole-archive")
    endif ()
    target_compile_options(yozora-skia PRIVATE -Wall -Wextra -Werror)

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(yozora-memory
            "${yozora_root}/tests/test_webcore_memory.cpp"
            "${YOZORA_WEBCORE_ROOT}/SharedMemoryLinux.cpp"
            "${WEBCORE_DIR}/platform/SharedMemory.cpp"
            "${WEBCORE_DIR}/platform/SharedBuffer.cpp"
            "${WEBCORE_DIR}/platform/skia/SharedBufferSkia.cpp")
        target_include_directories(yozora-memory PRIVATE
            "${CMAKE_BINARY_DIR}"
            "${WEBCORE_DIR}" "${WEBCORE_DIR}/platform"
            "${WEBCORE_DIR}/platform/graphics" "${WEBCORE_DIR}/platform/graphics/skia"
            "${WEBCORE_DIR}/platform/network" "${WEBCORE_DIR}/platform/network/curl"
            "${JavaScriptCore_FRAMEWORK_HEADERS_DIR}" "${JavaScriptCore_PRIVATE_FRAMEWORK_HEADERS_DIR}"
            "${WebCore_PRIVATE_FRAMEWORK_HEADERS_DIR}"
            "${Skia_FRAMEWORK_HEADERS_DIR}/top" "${Skia_FRAMEWORK_HEADERS_DIR}/mod")
        add_dependencies(yozora-memory WebCore_CopyPrivateHeaders
            JavaScriptCore_CopyHeaders JavaScriptCore_CopyPrivateHeaders)
        # WebKit aliases omit object-library code, use the framework link helper
        set(yozora-memory_FRAMEWORKS PAL WTF bmalloc)
        _WEBKIT_TARGET_LINK_FRAMEWORK(yozora-memory)
        target_link_libraries(yozora-memory PRIVATE ${yozora-memory_PRIVATE_LIBRARIES} Skia::Skia)
        # shared-memory tests retain only the exercised shared-buffer paths
        target_link_options(yozora-memory PRIVATE "LINKER:--gc-sections")
        target_compile_options(yozora-memory PRIVATE -Wall -Wextra -Werror)

        add_executable(yozora-raster
            "${yozora_root}/tests/test_webcore_raster.cpp"
            "${WEBCORE_DIR}/platform/graphics/DestinationColorSpace.cpp"
            "${WEBCORE_DIR}/platform/graphics/skia/ColorSkia.cpp"
            "${WEBCORE_DIR}/platform/graphics/skia/ColorSpaceSkia.cpp"
            "${WEBCORE_DIR}/platform/graphics/skia/SkiaUtilities.cpp")
        get_target_property(yozora_raster_includes yozora-memory INCLUDE_DIRECTORIES)
        target_include_directories(yozora-raster PRIVATE ${yozora_raster_includes})
        add_dependencies(yozora-raster WebCore_CopyPrivateHeaders
            JavaScriptCore_CopyHeaders JavaScriptCore_CopyPrivateHeaders)
        set(yozora-raster_FRAMEWORKS PAL WTF bmalloc)
        _WEBKIT_TARGET_LINK_FRAMEWORK(yozora-raster)
        target_link_libraries(yozora-raster PRIVATE ${yozora-raster_PRIVATE_LIBRARIES} Skia::Skia Threads::Threads)
        target_compile_options(yozora-raster PRIVATE -Wall -Wextra -Werror)
        target_link_options(yozora-raster PRIVATE "LINKER:--gc-sections")

        add_library(yozora-raster-platform OBJECT
            "${WEBCORE_DIR}/platform/graphics/ImageBuffer.cpp"
            "${WEBCORE_DIR}/platform/graphics/skia/GraphicsContextSkia.cpp"
            "${WEBCORE_DIR}/platform/graphics/skia/ImageBufferSkiaSurfaceBackend.cpp"
            "${WEBCORE_DIR}/platform/graphics/skia/ImageBufferSkiaUnacceleratedBackend.cpp"
            "${WEBCORE_DIR}/platform/graphics/skia/ImageUtilitiesSkia.cpp"
            "${WEBCORE_DIR}/platform/graphics/skia/PatternSkia.cpp")
        add_library(yozora-raster-images OBJECT
            "${WEBCORE_DIR}/platform/graphics/NativeImage.cpp"
            "${WEBCORE_DIR}/platform/graphics/skia/NativeImageSkia.cpp")
        get_target_property(yozora_platform_includes WebCore INCLUDE_DIRECTORIES)
        get_target_property(yozora_platform_defines WebCore COMPILE_DEFINITIONS)
        foreach (yozora_target IN ITEMS yozora-raster-platform yozora-raster-images)
            target_include_directories(${yozora_target} PRIVATE ${yozora_raster_includes} ${yozora_platform_includes})
            if (yozora_platform_defines)
                target_compile_definitions(${yozora_target} PRIVATE ${yozora_platform_defines})
            endif ()
            add_dependencies(${yozora_target} WebCore_CopyPrivateHeaders
                JavaScriptCore_CopyHeaders JavaScriptCore_CopyPrivateHeaders)
            target_link_libraries(${yozora_target} PRIVATE WebKit::WTF WebKit::PAL Skia::Skia HarfBuzz::HarfBuzz)
        endforeach ()
        target_sources(yozora-raster PRIVATE $<TARGET_OBJECTS:yozora-raster-images>)

        add_executable(yozora-fonts
            "${yozora_root}/tests/test_webcore_fonts.cpp"
            "${YOZORA_WEBCORE_ROOT}/FontManagerYozora.cpp"
            "${YOZORA_WEBCORE_ROOT}/FontCacheYozora.cpp")
        target_include_directories(yozora-fonts PRIVATE ${yozora_raster_includes} "${YOZORA_WEBCORE_ROOT}")
        add_dependencies(yozora-fonts WebCore_CopyPrivateHeaders)
        set(yozora-fonts_FRAMEWORKS PAL WTF bmalloc)
        _WEBKIT_TARGET_LINK_FRAMEWORK(yozora-fonts)
        target_link_libraries(yozora-fonts PRIVATE ${yozora-fonts_PRIVATE_LIBRARIES} Skia::Skia Threads::Threads ICU::uc ICU::data)
        target_compile_options(yozora-fonts PRIVATE -Wall -Wextra -Werror)
        add_library(yozora-font-platform OBJECT "${WEBCORE_DIR}/platform/graphics/skia/FontCacheSkia.cpp")
        target_include_directories(yozora-font-platform PRIVATE ${yozora_raster_includes} ${yozora_platform_includes} "${YOZORA_WEBCORE_ROOT}")
        if (yozora_platform_defines)
            target_compile_definitions(yozora-font-platform PRIVATE ${yozora_platform_defines})
        endif ()
        add_dependencies(yozora-font-platform WebCore_CopyPrivateHeaders)
        target_link_libraries(yozora-font-platform PRIVATE WebKit::WTF WebKit::PAL Skia::Skia HarfBuzz::HarfBuzz)

        set(yozora_test_api "${CMAKE_SOURCE_DIR}/Tools/TestWebKitAPI/Tests/WebCore")
        configure_file("${WEBCORE_DIR}/testing/TestPage.h"
            "${CMAKE_BINARY_DIR}/yozora-test-headers/WebCoreTestSupport/TestPage.h" COPYONLY)
        add_executable(yozora-page
            "${yozora_root}/tests/test_webcore_page.cpp"
            "${yozora_root}/tests/test_webcore_services.cpp"
            "${yozora_root}/tests/yozora_test_page.cpp"
            "${yozora_test_api}/TestPlatformStrategies.cpp"
            "${yozora_root}/core/yozora_log.c")
        target_include_directories(yozora-page PRIVATE ${yozora_raster_includes} ${yozora_platform_includes}
            "${YOZORA_WEBCORE_ROOT}" "${yozora_test_api}" "${CMAKE_BINARY_DIR}/yozora-test-headers")
        set(yozora-page_FRAMEWORKS WebCore JavaScriptCore PAL WTF bmalloc)
        _WEBKIT_TARGET_LINK_FRAMEWORK(yozora-page)
        target_link_libraries(yozora-page PRIVATE ${yozora-page_PRIVATE_LIBRARIES})
        get_filename_component(yozora_host_deps "${YOZORA_ICU_ROOT}/.." ABSOLUTE)
        # host HarfBuzz uses the isolated Yozora C++ namespace behind its C API
        set_property(TARGET HarfBuzz::HarfBuzz APPEND PROPERTY INTERFACE_LINK_LIBRARIES
            "${yozora_host_deps}/runtime/lib/libc++.a" "${yozora_host_deps}/runtime/lib/libc++abi.a")
        target_link_options(yozora-page PRIVATE "LINKER:--gc-sections")

        add_executable(yozora-page-adapter-sanitize EXCLUDE_FROM_ALL
            "${yozora_root}/tests/test_webcore_page.cpp"
            "${yozora_root}/tests/test_webcore_services.cpp"
            "${yozora_root}/tests/yozora_test_page.cpp"
            "${yozora_test_api}/TestPlatformStrategies.cpp"
            "${YOZORA_WEBCORE_ROOT}/MIMETypeRegistryYozora.cpp"
            "${YOZORA_WEBCORE_ROOT}/PlatformScreenYozora.cpp"
            "${YOZORA_WEBCORE_ROOT}/SystemFontDatabaseYozora.cpp"
            "${YOZORA_WEBCORE_ROOT}/GraphicsLayerYozora.cpp"
            "${YOZORA_WEBCORE_ROOT}/PasteboardYozora.cpp"
            "${YOZORA_WEBCORE_ROOT}/EditorYozora.cpp"
            "${YOZORA_WEBCORE_ROOT}/WebContentReaderYozora.cpp"
            "${YOZORA_WEBCORE_ROOT}/KillRingYozora.cpp"
            "${YOZORA_WEBCORE_ROOT}/SoundYozora.cpp")
        get_target_property(yozora_page_includes yozora-page INCLUDE_DIRECTORIES)
        target_include_directories(yozora-page-adapter-sanitize PRIVATE ${yozora_page_includes})
        set(yozora-page-adapter-sanitize_FRAMEWORKS WebCore JavaScriptCore PAL WTF bmalloc)
        _WEBKIT_TARGET_LINK_FRAMEWORK(yozora-page-adapter-sanitize)
        target_link_libraries(yozora-page-adapter-sanitize PRIVATE ${yozora-page-adapter-sanitize_PRIVATE_LIBRARIES})
        target_compile_options(yozora-page-adapter-sanitize PRIVATE
            -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
        target_link_options(yozora-page-adapter-sanitize PRIVATE
            -fsanitize=address,undefined "LINKER:--gc-sections")

        add_executable(yozora-platform "${yozora_root}/tests/test_webcore_platform.cpp")
        target_include_directories(yozora-platform PRIVATE ${yozora_raster_includes} ${yozora_platform_includes})
        add_dependencies(yozora-platform WebCore)
        set(yozora-platform_FRAMEWORKS PAL WTF bmalloc)
        _WEBKIT_TARGET_LINK_FRAMEWORK(yozora-platform)
        # this portable probe retains locale code from the main archive, without a DOM page
        target_link_libraries(yozora-platform PRIVATE "$<TARGET_FILE:WebCore>"
            ${yozora-platform_PRIVATE_LIBRARIES} "$<TARGET_PROPERTY:WebCore,LINK_LIBRARIES>")
        target_link_options(yozora-platform PRIVATE "LINKER:--gc-sections")
    endif ()
endfunction()

cmake_language(DEFER CALL yozora_add_graphics_probe)

include("${CMAKE_CURRENT_LIST_DIR}/Core.cmake")
