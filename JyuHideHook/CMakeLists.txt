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
