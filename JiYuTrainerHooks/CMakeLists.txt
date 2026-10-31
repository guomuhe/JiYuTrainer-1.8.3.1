# =============================================================================
# JiYuTrainerHooks - DLL (API hooking using mhook + disasm-lib)
# =============================================================================

add_library(JiYuTrainerHooks SHARED
    dllmain.cpp
    JiYuTrainerHooks.cpp
    StringHlp.cpp
    StringSplit.cpp
    mhook-lib/mhook.cpp
    disasm-lib/cpu.c
    disasm-lib/disasm.c
    disasm-lib/disasm_x86.c
    disasm-lib/misc.c
    JiYuTrainerHooks.rc
)

target_include_directories(JiYuTrainerHooks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/mhook-lib
    ${CMAKE_CURRENT_SOURCE_DIR}/disasm-lib
)

target_compile_definitions(JiYuTrainerHooks PRIVATE
    JIYUKILLERVIRUS_EXPORTS
    _WINDOWS
    _USRDLL
)

target_link_libraries(JiYuTrainerHooks PRIVATE
    Version
    shlwapi
    kernel32
    user32
    advapi32
    shell32
    ole32
)

# 静态链接 MinGW 运行时，保证注入到极域进程时无外部依赖
target_link_options(JiYuTrainerHooks PRIVATE -static -static-libgcc -static-libstdc++)

set_target_properties(JiYuTrainerHooks PROPERTIES
    PREFIX ""
    SUFFIX ".dll"
)

# windres codepage: JiYuTrainerHooks.rc uses GBK-encoded Chinese strings
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --codepage=936")
