cmake_minimum_required(VERSION 3.18)

project(cutefish-debinstaller LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Quick LinguistTools Concurrent)

# 可选：查找 APT 库，但不强制要求
find_library(APT_PKG_LIBRARY 
    NAMES apt-pkg
    PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu
)

if(APT_PKG_LIBRARY)
    message(STATUS "Found APT library: ${APT_PKG_LIBRARY}")
    # 查找 APT 头文件目录
    find_path(APT_PKG_INCLUDE_DIR
        NAMES apt-pkg/configuration.h
        PATHS /usr/include /usr/include/apt-pkg
    )
    if(APT_PKG_INCLUDE_DIR)
        message(STATUS "Found APT include dir: ${APT_PKG_INCLUDE_DIR}")
    endif()
else()
    message(WARNING "APT library not found, using dpkg commands only")
endif()

set(PROJECT_SOURCES
    src/main.cpp
    src/debinstaller.cpp
    qml.qrc
)

add_executable(cutefish-debinstaller
    ${PROJECT_SOURCES}
)

target_link_libraries(cutefish-debinstaller PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Quick
    Qt6::Concurrent
)

# 如果找到 APT 库，则链接它
if(APT_PKG_LIBRARY AND APT_PKG_INCLUDE_DIR)
    target_link_libraries(cutefish-debinstaller PRIVATE ${APT_PKG_LIBRARY})
    target_include_directories(cutefish-debinstaller PRIVATE ${APT_PKG_INCLUDE_DIR})
endif()

# 翻译文件
file(GLOB TS_FILES translations/*.ts)
qt6_add_translation(QM_FILES ${TS_FILES})
add_custom_target(translations DEPENDS ${QM_FILES} SOURCES ${TS_FILES})
add_dependencies(cutefish-debinstaller translations)

# 安装目标
install(FILES ${QM_FILES} DESTINATION /usr/share/cutefish-debinstaller/translations)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
    cutefish-debinstaller.desktop
    DESTINATION /usr/share/applications/
    COMPONENT Runtime
)