# JyuHideHook - 轻量窗口隐蔽 DLL
# 给跨进程注入用，只有 5 个导出函数 + 空 DllMain
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(JyuHideHook SHARED
    JyuHideHook.cpp
)
target_compile_definitions(JyuHideHook PRIVATE BUILD_DLL)
target_link_libraries(JyuHideHook PRIVATE user32 dwmapi)
set_target_properties(JyuHideHook PROPERTIES
    PREFIX ""
    SUFFIX ".dll"
)

# JiYuTrainer.rc 以 ..\Release\JyuHideHook.dll 内嵌该 DLL，构建后同步一份到 Release
add_custom_command(TARGET JyuHideHook POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:JyuHideHook> ${CMAKE_SOURCE_DIR}/Release/JyuHideHook.dll
)
