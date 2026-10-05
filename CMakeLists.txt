cmake_minimum_required(VERSION 3.15)
project(cm108ptt CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OUTPUT_NAME "cm108ptt" CACHE STRING "Executable output name without extension")

option(USE_CRINKLER "Link with Crinkler compressing linker (MSVC)" OFF)
set(CRINKLER_PATH "crinkler.exe" CACHE FILEPATH "Path to crinkler.exe")


if(USE_CRINKLER AND NOT EXISTS "${CRINKLER_PATH}")
    find_program(CRINKLER_BIN NAMES crinkler crinkler.exe)
    if(CRINKLER_BIN)
        set(CRINKLER_PATH "${CRINKLER_BIN}" CACHE FILEPATH "Path to crinkler.exe" FORCE)
    endif()
endif()

option(USE_UPX "Compress binary with UPX post-build" OFF)
set(UPX_PATH "upx.exe" CACHE FILEPATH "Path to upx.exe")

if(USE_UPX AND NOT EXISTS "${UPX_PATH}")
    find_program(UPX_BIN NAMES upx upx.exe)
    if(UPX_BIN)
        set(UPX_PATH "${UPX_BIN}" CACHE FILEPATH "Path to upx.exe" FORCE)
    endif()
endif()

option(TARGET_WINXP "Target Windows XP 32-bit compatibility" OFF)
option(ENABLE_ICON "Embed application icon" OFF)


set(SOURCES
    src/main.cpp
    src/config.cpp
    src/cm108.cpp
    src/serial_monitor.cpp
)

set(HEADERS
    src/config.h
    src/cm108.h
    src/serial_monitor.h
    src/resource.h
)

set(WIN32_LIBS
    kernel32
    user32
    gdi32
    comctl32
    advapi32
    setupapi
    hid
)

if(MSVC AND USE_CRINKLER)
    message(STATUS "Configuring MSVC build with Crinkler linker: ${CRINKLER_PATH}")

    add_library(cm108ptt_objs OBJECT ${SOURCES} ${HEADERS})

    target_compile_definitions(cm108ptt_objs PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        USE_CRINKLER
    )

    if(TARGET_WINXP)
        target_compile_definitions(cm108ptt_objs PRIVATE _WIN32_WINNT=0x0501 WINVER=0x0501)
        set(CRINKLER_SUBSYSTEM "/SUBSYSTEM:WINDOWS,5.01")
        set(CRINKLER_RANGE "/RANGE:XP")
    else()
        target_compile_definitions(cm108ptt_objs PRIVATE _WIN32_WINNT=0x0601 WINVER=0x0601)
        set(CRINKLER_SUBSYSTEM "/SUBSYSTEM:WINDOWS")
        set(CRINKLER_RANGE "")
    endif()

    target_compile_options(cm108ptt_objs PRIVATE
        /O1             # Minimize size
        /Os             # Favor small code
        /Oy             # Omit frame pointer
        /Oi             # Enable intrinsic functions
        /Gy             # Separate functions for linker GC
        /Gw             # Optimize global data
        /GS-            # Disable buffer security check (no CRT dependency)
        /GR-            # Disable RTTI
        /EHs-c-         # Disable C++ exceptions
    )

    if(CMAKE_CONFIGURATION_TYPES)
        set(CRINKLER_DIR "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
    else()
        set(CRINKLER_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    set(CRINKLER_OUT "${CRINKLER_DIR}/${OUTPUT_NAME}.exe")

    set(CRINKLER_ARGS
        "/OUT:${CRINKLER_OUT}"
        ${CRINKLER_SUBSYSTEM}
        /COMPMODE:SLOW
        /PROGRESSGUI
        /ENTRY:WinMainEntry
    )
    if(TARGET_WINXP)
        list(APPEND CRINKLER_ARGS "/RANGE:XP")
    endif()

    set(CRINKLER_LIBS
        kernel32.lib
        user32.lib
        gdi32.lib
        comctl32.lib
        advapi32.lib
        setupapi.lib
        hid.lib
    )

    add_custom_command(
        OUTPUT "${CRINKLER_OUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CRINKLER_DIR}"
        COMMAND "${CRINKLER_PATH}"
                ${CRINKLER_ARGS}
                $<TARGET_OBJECTS:cm108ptt_objs>
                ${CRINKLER_LIBS}
        DEPENDS cm108ptt_objs
        COMMAND_EXPAND_LISTS
        COMMENT "Linking and compressing ${OUTPUT_NAME}.exe with Crinkler..."
        VERBATIM
    )

    add_custom_target(cm108ptt ALL DEPENDS "${CRINKLER_OUT}")

else()

    set(BUILD_SOURCES ${SOURCES} ${HEADERS})
    if(ENABLE_ICON)
        list(APPEND BUILD_SOURCES src/cm108ptt.rc)
    endif()

    add_executable(cm108ptt WIN32 ${BUILD_SOURCES})
    set_target_properties(cm108ptt PROPERTIES OUTPUT_NAME "${OUTPUT_NAME}")

    target_compile_definitions(cm108ptt PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )

    if(MSVC)
        if(TARGET_WINXP)
            target_compile_definitions(cm108ptt PRIVATE _WIN32_WINNT=0x0501 WINVER=0x0501)
            set(SUBSYSTEM_VER "5.01")
        else()
            target_compile_definitions(cm108ptt PRIVATE _WIN32_WINNT=0x0601 WINVER=0x0601)
            set(SUBSYSTEM_VER "6.00")
        endif()

        target_compile_options(cm108ptt PRIVATE
            /O1
            /Os
            /Oy
            /GL
            /Gy
            /Gw
            /GS-
            /GR-
            /EHs-c-
        )

        target_link_options(cm108ptt PRIVATE
            /LTCG
            /OPT:REF
            /OPT:ICF
            /SUBSYSTEM:WINDOWS,${SUBSYSTEM_VER}
        )

        target_link_libraries(cm108ptt PRIVATE ${WIN32_LIBS})

    elseif(MINGW OR CMAKE_CROSSCOMPILING)
        if(TARGET_WINXP)
            target_compile_definitions(cm108ptt PRIVATE _WIN32_WINNT=0x0501 WINVER=0x0501)
        else()
            target_compile_definitions(cm108ptt PRIVATE _WIN32_WINNT=0x0601 WINVER=0x0601)
        endif()

        target_compile_options(cm108ptt PRIVATE
            -Os
            -s
            -fno-exceptions
            -fno-rtti
            -ffunction-sections
            -fdata-sections
        )

        target_link_options(cm108ptt PRIVATE
            -mwindows
            -Wl,--gc-sections
            -static
            -static-libgcc
            -static-libstdc++
        )

        target_link_libraries(cm108ptt PRIVATE ${WIN32_LIBS})
    endif()

    if(USE_UPX)
        message(STATUS "Adding UPX compression post-build step")
        add_custom_command(TARGET cm108ptt POST_BUILD
            COMMAND "${UPX_PATH}" --best "$<TARGET_FILE:cm108ptt>"
            COMMENT "Packing ${OUTPUT_NAME} with UPX"
        )
    endif()

endif()
