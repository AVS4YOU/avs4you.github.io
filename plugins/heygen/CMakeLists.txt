cmake_minimum_required(VERSION 3.5)
project(Heygen)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(TARGET_ARCH "x86")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TARGET_ARCH "x64")
else()
    message(FATAL_ERROR "Unknown architecture: CMAKE_SIZEOF_VOID_P = ${CMAKE_SIZEOF_VOID_P}")
endif()

add_definitions(-DVISUALUI_STATIC -DPLUGIN_EXPORTS)
set(SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk)
set(COMMON_ROOT ${SDK_ROOT}/common)
set(3DPARTY_ROOT ${SDK_ROOT}/3dParty)
set(STATIC_ARCH_DIR "${TARGET_ARCH}-windows-static")

set(3DPARTY_STATIC_ROOT "${3DPARTY_ROOT}/${STATIC_ARCH_DIR}")
set(3DPARTY_STATIC_INCLUDE "${3DPARTY_STATIC_ROOT}/include")
set(3DPARTY_STATIC_LIB "${3DPARTY_STATIC_ROOT}/$<$<CONFIG:Debug>:debug>/lib")

set(LIBS_STATIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libs/${STATIC_ARCH_DIR}")
set(LIBS_STATIC_INCLUDE "${LIBS_STATIC_ROOT}/include")
set(LIBS_STATIC_LIB "${LIBS_STATIC_ROOT}/$<$<CONFIG:Debug>:debug>/lib")

set(VISUALUI_INCLUDE_DIR "${LIBS_STATIC_INCLUDE}/VisualUI")

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mainwindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/aboutdialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/apikeydialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CCore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CUserSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CAvatar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CVoice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/IHeygenPlugin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CHeygenPlugin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DSAudioPlayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ISLTranslator.cpp
)

set(HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/defines.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/resource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/version.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mainwindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui_mainwindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/aboutdialog.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui_aboutdialog.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/apikeydialog.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui_apikeydialog.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CCore.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CUserSettings.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CAvatar.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CVoice.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/IHeygenPlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CHeygenPlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DSAudioPlayer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ISLTranslator.h
)

set(EXTERNAL_SOURCES
    ${COMMON_ROOT}/ActivationContext.cpp
    ${COMMON_ROOT}/CHttpClient.cpp
    ${COMMON_ROOT}/CIconExtractor.cpp
)

set(EXTERNAL_HEADERS
    ${COMMON_ROOT}/ActivationContext.h
    ${COMMON_ROOT}/CHttpClient.h
    ${COMMON_ROOT}/HttpStatusCodes.h
    ${COMMON_ROOT}/CIconExtractor.h
    ${3DPARTY_ROOT}/nlohmann/json/single_include/nlohmann/json.hpp
)

set(OTHER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/res/styles/styles.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/res/styles/themes.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/res/langs/en.isl
    ${CMAKE_CURRENT_SOURCE_DIR}/res/langs/ru.isl
)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS} ${EXTERNAL_SOURCES} ${EXTERNAL_HEADERS})

target_compile_definitions(${PROJECT_NAME} PRIVATE VISUALUI_LIBRARY)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${COMMON_ROOT}
    ${SDK_ROOT}/include
    ${3DPARTY_ROOT}/nlohmann/json/single_include
    ${VISUALUI_INCLUDE_DIR}
    ${VISUALUI_INCLUDE_DIR}/core
    ${VISUALUI_INCLUDE_DIR}/common
    ${VISUALUI_INCLUDE_DIR}/graphics
    ${VISUALUI_INCLUDE_DIR}/layout
    ${VISUALUI_INCLUDE_DIR}/widgets
    ${VISUALUI_INCLUDE_DIR}/dialogs
    ${3DPARTY_STATIC_INCLUDE}
    ${LIBS_STATIC_INCLUDE}
)

if(TARGET_ARCH STREQUAL "x86")
    set(BUILD_SUBDIR "Win32")
else()
    set(BUILD_SUBDIR "x64")
endif()
set(OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Build/${BUILD_SUBDIR}/$<CONFIG>")

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "Heygen"
    ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
)

if (WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _UNICODE UNICODE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "${LIBS_STATIC_LIB}/visualui.lib"
        "${LIBS_STATIC_LIB}/libwebp.lib"
        "${3DPARTY_STATIC_LIB}/libcurl.lib"
        "${3DPARTY_STATIC_LIB}/zlib.lib"
        gdiplus shlwapi comctl32 winmm
    )

    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/module.def /MANIFEST:NO"
    )

    set(OTHER_FILES
        ${OTHER_FILES}
        ${CMAKE_CURRENT_SOURCE_DIR}/res/manifest/Heygen.dll.manifest
        ${CMAKE_CURRENT_SOURCE_DIR}/module.def
    )
    set_source_files_properties(
        ${OTHER_FILES}
        PROPERTIES HEADER_FILE_ONLY TRUE
    )
    set(RC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/res/resource.rc)
    if(TARGET_ARCH STREQUAL "x64")
        set_property(SOURCE ${RC_FILE} APPEND PROPERTY COMPILE_DEFINITIONS
            "$<$<CONFIG:Release>:RC_X64>"
        )
    endif()
    set(RESOURCE_FILES
        ${RC_FILE}
        ${OTHER_FILES}
    )
    target_sources(${PROJECT_NAME} PRIVATE ${RESOURCE_FILES})

endif()

source_group("Resource Files" FILES ${RESOURCE_FILES})
source_group("External Header Files" FILES ${EXTERNAL_HEADERS})
source_group("External Source Files" FILES ${EXTERNAL_SOURCES})
