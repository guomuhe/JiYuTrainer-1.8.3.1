# =============================================================================
# JiYuTrainer - Main executable (Windows subsystem)
# =============================================================================

set(MAIN_SOURCES
    CAutoLock.cpp
    DriverLoader.cpp
    JiYuTrainer.cpp
    App.cpp
    JyUdpAttack.cpp
    KernelUtils.cpp
    Logger.cpp
    md5.cpp
    MemoryModule.cpp
    MsgCenter.cpp
    NetUtils.cpp
    NtHlp.cpp
    NTDExt.cpp
    PathHelper.cpp
    MythwareExt.cpp
    RegHlp.cpp
    SettingHlp.cpp
    StringHlp.cpp
    StringSplit.cpp
    SysHlp.cpp
    TrainerWorker.cpp
    TxtUtils.cpp
    window_utils.cpp
    inject.cpp
    FloatingWindow.cpp
    XUnzip.cpp
    XZip.cpp
    JiYuTrainer.rc
)

add_executable(JiYuTrainer WIN32 ${MAIN_SOURCES})

target_include_directories(JiYuTrainer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../JiYuTrainerUI
    ${CMAKE_CURRENT_SOURCE_DIR}/../JiYuTrainerUI/sciter-sdk/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../JiYuTrainerUpdater
    ${CMAKE_CURRENT_SOURCE_DIR}/../JiYuTrainerUpdater/curl
)

target_compile_definitions(JiYuTrainer PRIVATE
    JTEXPORT
    _WINDOWS
)

target_link_libraries(JiYuTrainer PRIVATE
    JiYuTrainerUI
    JiYuTrainerUpdater
    ntdll
    iphlpapi
    mpr
    ws2_32
    comctl32
    wininet
    wldap32
    dbghelp
    shlwapi
    kernel32
    user32
    advapi32
    shell32
    ole32
    oleaut32
    uuid
    msimg32  # ★ GradientFill 渐变
)

# é™æ€é“¾æŽ?MinGW è¿è¡Œæ—¶ï¼Œä½?exe å¤åˆ¶å³ç”¨ï¼Œæ— éœ€æºå¸¦ libgcc/libstdc++/libwinpthread
target_link_options(JiYuTrainer PRIVATE -static -static-libgcc -static-libstdc++)

set_target_properties(JiYuTrainer PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "JiYuTrainer"
)

