CMAKE_MINIMUM_REQUIRED(VERSION 3.18)
PROJECT(hiktrack CXX)

SET(CMAKE_CXX_STANDARD 20)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置静态链接运行时库
IF(MSVC)
  # 静态链接MSVC运行时库
  SET(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
  SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
ELSE()
  # GCC/Clang静态链接
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
ENDIF()

# 源文件
SET(HIKTRACK_SOURCES
    track/hiktrack.cc
    track/hiktrack.hh
    my_track.hh
    hiktrack.def
)
INCLUDE_DIRECTORIES(.)
# 创建动态库
ADD_LIBRARY(hiktrack SHARED ${HIKTRACK_SOURCES})
# 1. 随便建一个目标（可以是接口库）
#ADD_LIBRARY(hiktrack INTERFACE)

# 2. 把 include 目录挂到接口上
TARGET_INCLUDE_DIRECTORIES(hiktrack INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
# 设置输出名称
SET_TARGET_PROPERTIES(hiktrack PROPERTIES
    OUTPUT_NAME "hiktrack"
    VERSION 1.0
    SOVERSION 1
)

# 编译定义
TARGET_COMPILE_DEFINITIONS(hiktrack PRIVATE
    HIKTRACK_EXPORTS
    MI_TRACK_MYHOOK
)

IF(WIN32)
  # Windows平台设置
  TARGET_COMPILE_DEFINITIONS(hiktrack PRIVATE
      WIN32_LEAN_AND_MEAN
      NOMINMAX
      _CRT_SECURE_NO_WARNINGS
  )

  # 使用DEF文件导出符号
  SET_TARGET_PROPERTIES(hiktrack PROPERTIES
      LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/hiktrack.def"
  )
ENDIF()

# 编译选项
IF(MSVC)
  TARGET_COMPILE_OPTIONS(hiktrack PRIVATE
      /W3          # 警告级别
      /permissive- # 严格模式
      /Zc:__cplusplus # 正确的__cplusplus宏
  )
ELSE()
  TARGET_COMPILE_OPTIONS(hiktrack PRIVATE
      -Wall
      -Wextra
      -fPIC
  )
ENDIF()

# 包含目录
TARGET_INCLUDE_DIRECTORIES(hiktrack PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# 链接库
IF(WIN32)
  TARGET_LINK_LIBRARIES(hiktrack PRIVATE
      kernel32
      user32
  )
ELSE()
  TARGET_LINK_LIBRARIES(hiktrack PRIVATE
      pthread
  )
ENDIF()

# 安装规则
INSTALL(TARGETS hiktrack
    EXPORT HiktrackTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

INSTALL(FILES
    my_track.hh
    track/hiktrack.hh
    DESTINATION include/hiktrack
)

# 导出配置
INSTALL(EXPORT HiktrackTargets
    FILE HiktrackTargets.cmake
    NAMESPACE Hiktrack::
    DESTINATION lib/cmake/hiktrack
)