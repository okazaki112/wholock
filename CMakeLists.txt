cmake_minimum_required(VERSION 3.28)
project(WhoLock VERSION 1.0.0 LANGUAGES CXX)

# F2 版本单一事实源（v1.0 迭代）：project(VERSION) 唯一手写点 → 生成 version.h
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in"
               "${CMAKE_BINARY_DIR}/generated/version.h" @ONLY)

if(NOT WIN32)
  message(FATAL_ERROR "WhoLock 仅支持 Windows（Win32 原生 API）")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_definitions(UNICODE _UNICODE NOMINMAX)
# 单一事实源:Release=静态CRT(/MT) → 单 exe 免装 VC 运行时; Debug=静态调试CRT
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc /Zc:__cplusplus /guard:cf-)
  # 注:/guard:cf- 显式关闭以保留 M1 构建速度，M5 评估是否重新开启并在 README 记录
endif()

# ---- 分层（依赖严格单向 domain ← application；testing 供桩）----
add_library(who_domain STATIC
  src/domain/Normalize.cpp src/domain/Merge.cpp src/domain/RiskFlags.cpp src/domain/CliArgs.cpp)
target_include_directories(who_domain PUBLIC src)

add_library(who_app STATIC src/application/ScanCoordinator.cpp)
target_link_libraries(who_app PUBLIC who_domain)

add_library(who_test STATIC src/testing/StubScanner.cpp)
target_link_libraries(who_test PUBLIC who_domain)

# ---- infrastructure/win32：真引擎与系统能力（只依赖 domain）----
add_library(who_infra STATIC
  src/infrastructure/win32/RmScanner.cpp
  src/infrastructure/win32/HandleTableScanner.cpp
  src/infrastructure/win32/WindowsPathMap.cpp
  src/infrastructure/win32/Privileges.cpp
  src/infrastructure/win32/ProcessOps.cpp
  src/infrastructure/win32/ProcessQuery.cpp
  src/infrastructure/win32/PrepareTargets.cpp
  src/infrastructure/win32/DeepScanRunner.cpp
  src/infrastructure/win32/DeepScanner.cpp
  src/infrastructure/win32/Settings.cpp
  src/infrastructure/win32/ShellIntegration.cpp
  src/infrastructure/win32/Engines.cpp)
target_link_libraries(who_infra PUBLIC who_domain rstrtmgr advapi32)

# ---- 主程序（win32 UI）+ CLI 模态（同体 exe）----
add_executable(wholock WIN32
  src/ui_main.cpp
  src/ui/Theme.cpp
  src/cli/CliMain.cpp
  src/app.rc)
if(MSVC)
  # 嵌入清单（Common-Controls v6 + PerMonitorV2）：不依赖外置 .manifest 文件，保单 exe 免装
  set_property(TARGET wholock PROPERTY LINK_FLAGS
    "/MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest")
endif()
target_link_libraries(wholock PRIVATE who_app who_infra who_domain)
target_include_directories(wholock PRIVATE "${CMAKE_BINARY_DIR}/generated")

# ---- 测试（R8-c：标准库自写 harness；ctest 一条命令）----
enable_testing()
add_executable(unit_tests tests/unit/main_tests.cpp)
target_link_libraries(unit_tests PRIVATE who_domain)
add_test(NAME unit_tests COMMAND unit_tests)

add_executable(integration_engine_swap tests/integration/engine_swap.cpp)
target_link_libraries(integration_engine_swap PRIVATE who_app who_test)
add_test(NAME integration_engine_swap COMMAND integration_engine_swap)

# G1 独立判据：自锁真跑（OS 语义为尺子）
add_executable(integration_self_lock tests/integration/self_lock.cpp)
target_link_libraries(integration_self_lock PRIVATE who_infra who_app)
add_test(NAME integration_self_lock COMMAND integration_self_lock)

# G6 硬链接身份等价类（FileIdInfo 为尺 + 负对照防恒真）
add_executable(integration_identity tests/integration/identity_hardlink.cpp)
target_link_libraries(integration_identity PRIVATE who_infra who_domain)
add_test(NAME integration_identity COMMAND integration_identity)

# F12 右键集成往返（HKCU 真跑）
add_executable(integration_shell tests/integration/shell_integration.cpp)
target_link_libraries(integration_shell PRIVATE who_infra)
add_test(NAME integration_shell COMMAND integration_shell)

# F3 设置持久化往返 + 损坏 ini 防御
add_executable(integration_settings tests/integration/settings_roundtrip.cpp)
target_link_libraries(integration_settings PRIVATE who_infra who_domain)
add_test(NAME integration_settings COMMAND integration_settings)

# G4 目录目标：递归展开 + RM 多目标 + HT occupiedFile 归属 + 空/不存在目录负样例
add_executable(integration_dir tests/integration/dir_lock.cpp)
target_link_libraries(integration_dir PRIVATE who_infra who_app)
add_test(NAME integration_dir COMMAND integration_dir)

# M8 判据：F6 深扫补盲(只读占用 RM 看不见 vs 深扫可见) + F8 关句柄往返 + F11 删除验证 + 取消杀兜底
add_executable(integration_release tests/integration/release_close.cpp)
target_link_libraries(integration_release PRIVATE who_infra who_app)
add_test(NAME integration_release COMMAND integration_release)

# release 产物统一输出目录，方便 dumpbin 核对
set_target_properties(wholock unit_tests integration_engine_swap integration_self_lock integration_identity integration_settings integration_dir integration_release integration_shell PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/out")
