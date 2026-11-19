cmake_minimum_required(VERSION 3.16)
project(JiYuTrainer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 全局编译选项
add_compile_options(
    -Wall
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
    -Wno-maybe-uninitialized
    -finput-charset=UTF-8
    -fexec-charset=UTF-8
    -fno-keep-inline-functions
    -fpermissive
    -fms-extensions
)

# MSVC 安全函数兼容头 - 在每个 stdafx.h 里 #include <windows.h> 之后手工 include
# （不能用 -include，因为 GWL_* 常量需要在 <windows.h> 处理完之后定义）

# 资源编译器编码：所有 .rc 已统一为 UTF-8 (+BOM)
if(MINGW)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --codepage=65001")
elseif(MSVC)
    # MSVC rc.exe 看到 UTF-8 BOM 会自动按 UTF-8 解读，无需额外 flag
endif()

# MSVC 编译器强制 UTF-8 解读 .cpp/.h（防止无 BOM 源码变乱码）
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 注入到其它进程的 DLL 不能依赖 libgcc/libstdc++/winpthread 运行库
# （目标进程目录里没有这些 DLL，LoadLibrary 会失败 126）
if(MINGW)
    add_link_options(-static)
endif()

# 全局预处理器定义
add_compile_definitions(
    WIN32
    _WIN32
    UNICODE
    _UNICODE
    NOMINMAX
    CINTERFACE
)

# 子项目顺序（按依赖关系）：
# 1. JiYuTrainerUpdater (静态库，含 curl)
# 2. JiYuTrainerHooks (DLL)
# 3. JyuHideHook (轻量窗口隐蔽 DLL)
# 4. JiYuTrainerUI (静态库 Release|Win32)
# 5. JiYuTrainer (EXE)

add_subdirectory(JiYuTrainerUpdater)
add_subdirectory(JiYuTrainerHooks)
add_subdirectory(JyuHideHook)
add_subdirectory(JiYuTrainerUI)
add_subdirectory(JiYuTrainer)
