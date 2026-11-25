# ============================================================================
# SpeedyNote-SyncMod (库 B) - 同步模块
# ============================================================================
# 独立静态库，被库 A（SpeedyNote-PluginHost）在编译期链接。
# 只依赖 Qt6 Core + Network，不依赖 SpeedyNote 内部头文件。
# 这样上游 SpeedyNote 怎么重构，本库都不受影响。
# ============================================================================

cmake_minimum_required(VERSION 3.16)
project(SpeedyNoteSyncMod LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network)

set(SYNCMOD_SOURCES
    src/SyncConfig.cpp
    src/SyncManager.cpp
    backends/WebDavBackend.cpp
    backends/OneDriveBackend.cpp
)

set(SYNCMOD_HEADERS
    include/SyncBackend.h
    include/SyncConfig.h
    include/SyncManager.h
    backends/WebDavBackend.h
    backends/OneDriveBackend.h
)

add_library(SpeedyNoteSyncMod STATIC
    ${SYNCMOD_SOURCES}
    ${SYNCMOD_HEADERS}
)

target_include_directories(SpeedyNoteSyncMod PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/backends
)

target_link_libraries(SpeedyNoteSyncMod PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
)
