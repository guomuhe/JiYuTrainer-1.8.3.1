# =============================================================================
# JiYuTrainerUI - Static library (Release|Win32 configuration)
#
# In the original vcxproj:
#   - Debug|Win32  : StaticLibrary
#   - Release|Win32: StaticLibrary
#   - Debug|x64    : DynamicLibrary
#   - Release|x64  : DynamicLibrary
# We build as STATIC to break the circular dependency with JiYuTrainer EXE.
# =============================================================================

set(SITER_SOURCES
    AttackWindow.cpp
    BugReportWindow.cpp
    CommonWindow.cpp
    ConfigWindow.cpp
    HelpWindow.cpp
    JiYuTrainerUI.cpp
    MainWindow.cpp
    ScanIPWindow.cpp
    UpdaterWindow.cpp
)

add_library(JiYuTrainerUI STATIC ${SITER_SOURCES})

target_include_directories(JiYuTrainerUI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/sciter-sdk/include
)

target_compile_definitions(JiYuTrainerUI PRIVATE
    JIYUTRAINERUI_EXPORTS
    _WINDOWS
    _USRDLL
)

# 注：sciter-sdk/libsciter.a 是 64-bit 静态库，本项目当前构建为 32-bit (pei-i386)，
#     无法链接该库。Sciter 通过 sciter.dll + LoadLibrary/GetProcAddress 运行时加载，
#     libsciter.a 对 32-bit 构建无意义，此处已移除。
target_link_libraries(JiYuTrainerUI PUBLIC
    JiYuTrainerUpdater
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

# JiYuTrainerUI 原始设计是 DLL，有 Source.def 导出定义。
# 这里编译为静态库，不再需要 .def 文件。
