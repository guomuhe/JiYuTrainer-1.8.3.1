# =============================================================================
# JiYuTrainerUpdater - Static library containing libcurl + updater logic
# =============================================================================

# 收集 curl 源文件（不依赖 curl 的 CMakeLists.txt，直接用 config-win32.h）
file(GLOB CURL_C_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/curl/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/curl/vtls/*.c"
)

# JiYuTrainerUpdater 自身源文件
set(UPDATER_SELF_SOURCES
    JiYuTrainerUpdater.cpp
    NetUtils.cpp
)

add_library(JiYuTrainerUpdater STATIC
    ${UPDATER_SELF_SOURCES}
    ${CURL_C_SOURCES}
)

target_include_directories(JiYuTrainerUpdater PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/curl
)

target_compile_definitions(JiYuTrainerUpdater PRIVATE
    CURL_STATICLIB
    BUILDING_LIBCURL
    JTUPDATE_EXPORT
    _WINDOWS
    # MinGW's winsock2.h provides struct pollfd; tell curl not to redefine it
    HAVE_STRUCT_POLLFD=1
)

target_link_libraries(JiYuTrainerUpdater PUBLIC
    ws2_32
    wininet
    wldap32
    shlwapi
    kernel32
    user32
    advapi32
    shell32
    ole32
)

# curl 的 C 文件可能有更多警告，放宽一下
target_compile_options(JiYuTrainerUpdater PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wno-all -Wno-unused-function -Wno-unused-variable -Wno-sign-compare -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -Wno-dll-attribute-on-redeclaration -Wno-null-pointer-arithmetic -Wno-expansion-to-defined>
)
