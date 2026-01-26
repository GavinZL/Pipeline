cmake_minimum_required(VERSION 3.16)
project(Pipeline VERSION 1.0.0 LANGUAGES CXX)

# macOS/iOS 需要 Objective-C++ 支持
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 编辑器跳转
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================
# 选项
# ============================================
option(PIPELINE_BUILD_EXAMPLES "Build examples" OFF)
option(PIPELINE_BUILD_TESTS "Build tests" OFF)

# ============================================
# Submodules（作为依赖引入）
# ============================================

# LREngine submodule
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LREngine/CMakeLists.txt")
    message(STATUS "Found LREngine submodule")
    add_subdirectory(third_party/LREngine)
else()
    message(WARNING "LREngine submodule not found, please run: git submodule update --init")
endif()

# TaskQueue submodule
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/TaskQueue/TaskQueue/CMakeLists.txt")
    message(STATUS "Found TaskQueue submodule")
    add_subdirectory(third_party/TaskQueue/TaskQueue)
else()
    message(WARNING "TaskQueue submodule not found, please run: git submodule update --init")
endif()

# libyuv submodule (用于 YUV/RGB 格式转换)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libyuv/CMakeLists.txt")
    message(STATUS "Found libyuv submodule")
    set(LIBYUV_BUILD_TESTS OFF CACHE BOOL "Build libyuv tests" FORCE)
    add_subdirectory(third_party/libyuv)
else()
    message(WARNING "libyuv submodule not found, please run: git submodule update --init")
endif()

# ============================================
# 核心源文件
# ============================================
set(PIPELINE_CORE_SOURCES
    # 数据层
    src/data/FramePacket.cpp
    src/data/FramePort.cpp
    
    # Entity层
    src/entity/ProcessEntity.cpp
    src/entity/GPUEntity.cpp
    src/entity/CPUEntity.cpp
    src/entity/CompositeEntity.cpp
    
    # 核心层
    src/core/PipelineGraph.cpp
    src/core/PipelineExecutor.cpp
    src/core/PipelineManager.cpp
    src/core/PipelineConfig.cpp
    
    # 资源池
    src/pool/FramePacketPool.cpp
    src/pool/TexturePool.cpp
    
    # 工具库
    src/utils/PipelineLog.cpp
)

# ============================================
# 平台抽象层源文件
# ============================================
set(PIPELINE_PLATFORM_SOURCES
    src/platform/PlatformContext.cpp
)

if(ANDROID)
    list(APPEND PIPELINE_PLATFORM_SOURCES
        src/platform/AndroidEGLContextManager.cpp
    )
elseif(IOS OR APPLE)
    list(APPEND PIPELINE_PLATFORM_SOURCES
        src/platform/IOSMetalContextManager.mm
    )
endif()

# ============================================
# 扩展实体源文件
# ============================================
set(PIPELINE_EXTENDED_SOURCES
    src/entity/MergeEntity.cpp
)

# ============================================
# Input/Output 模块源文件
# ============================================
set(PIPELINE_IO_SOURCES
    src/input/InputEntity.cpp
    src/input/FrameSynchronizer.cpp
    src/output/OutputEntity.cpp
)

# 平台适配层源文件
if(ANDROID)
    list(APPEND PIPELINE_IO_SOURCES
        src/input/android/OESTextureInputStrategy.cpp
        src/output/android/AndroidEGLSurface.cpp
    )
elseif(IOS OR APPLE)
    list(APPEND PIPELINE_IO_SOURCES
        src/input/ios/PixelBufferInputStrategy.mm
        src/output/ios/iOSMetalSurface.mm
    )
endif()

# ============================================
# 外观接口源文件
# ============================================
set(PIPELINE_FACADE_SOURCES
    src/PipelineFacade.cpp
)

# ============================================
# 示例Entity源文件
# ============================================
set(PIPELINE_ENTITY_SOURCES
    entities/gpu/FilterEntity.cpp
    entities/gpu/BeautyEntity.cpp
    entities/cpu/FaceDetectionEntity.cpp
)

# ============================================
# 合并所有源文件
# ============================================
set(PIPELINE_SOURCES
    ${PIPELINE_CORE_SOURCES}
    ${PIPELINE_PLATFORM_SOURCES}
    ${PIPELINE_EXTENDED_SOURCES}
    ${PIPELINE_IO_SOURCES}
    ${PIPELINE_FACADE_SOURCES}
    ${PIPELINE_ENTITY_SOURCES}
)

# ============================================
# 创建Pipeline库
# ============================================
add_library(Pipeline STATIC ${PIPELINE_SOURCES})

target_include_directories(Pipeline
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ============================================
# 链接 Submodules
# ============================================

# 链接 LREngine
if(TARGET lrengine)
    target_link_libraries(Pipeline PUBLIC lrengine)
    message(STATUS "Pipeline linked with lrengine")
else()
    message(WARNING "lrengine target not found")
endif()

# 链接 TaskQueue (dispatch_queue)
if(TARGET dispatch_queue)
    target_link_libraries(Pipeline PUBLIC dispatch_queue)
    message(STATUS "Pipeline linked with dispatch_queue (TaskQueue)")
else()
    # 尝试手动包含TaskQueue头文件
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/TaskQueue/TaskQueue")
        target_include_directories(Pipeline PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/TaskQueue/TaskQueue
        )
        message(STATUS "TaskQueue headers included manually")
    else()
        message(WARNING "dispatch_queue target and TaskQueue headers not found")
    endif()
endif()

# 链接 libyuv (YUV/RGB 格式转换)
if(TARGET yuv)
    target_link_libraries(Pipeline PUBLIC yuv)
    target_include_directories(Pipeline PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libyuv/include
    )
    message(STATUS "Pipeline linked with libyuv")
else()
    message(WARNING "libyuv target not found")
endif()

# ============================================
# 编译选项
# ============================================
if(MSVC)
    target_compile_options(Pipeline PRIVATE /W4)
else()
    target_compile_options(Pipeline PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ============================================
# 平台特定配置
# ============================================
if(ANDROID)
    target_compile_definitions(Pipeline PRIVATE PIPELINE_PLATFORM_ANDROID)
    # 链接 Android 平台库
    target_link_libraries(Pipeline PUBLIC GLESv3 EGL android log)
    
elseif(IOS)
    target_compile_definitions(Pipeline PRIVATE PIPELINE_PLATFORM_IOS)
    # 链接 iOS 框架
    target_link_libraries(Pipeline PUBLIC
        "-framework Metal"
        "-framework CoreVideo"
        "-framework AVFoundation"
        "-framework QuartzCore"
    )
    # 设置 Objective-C++ 文件
    set_source_files_properties(
        ${PIPELINE_PLATFORM_SOURCES}
        PROPERTIES
        COMPILE_FLAGS "-x objective-c++"
    )
    
elseif(APPLE)
    target_compile_definitions(Pipeline PRIVATE PIPELINE_PLATFORM_MACOS)
    # 链接 macOS 框架
    target_link_libraries(Pipeline PUBLIC
        "-framework Metal"
        "-framework CoreVideo"
        "-framework QuartzCore"
    )
    
elseif(WIN32)
    target_compile_definitions(Pipeline PRIVATE PIPELINE_PLATFORM_WINDOWS)
    
else()
    target_compile_definitions(Pipeline PRIVATE PIPELINE_PLATFORM_LINUX)
endif()

# ============================================
# 安装配置
# ============================================
# 仅在作为顶级项目时启用，避免子项目模式下的 export 冲突
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    install(TARGETS Pipeline
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )

    # 安装头文件
    install(DIRECTORY include/pipeline
        DESTINATION include
        FILES_MATCHING PATTERN "*.h"
    )
    
    # 安装文档
    install(FILES
        ARCHITECTURE_DESIGN.md
        API_QUICK_REFERENCE.md
        README.md
        DESTINATION share/doc/Pipeline
    )
endif()

# ============================================
# 示例
# ============================================
if(PIPELINE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ============================================
# 测试
# ============================================
if(PIPELINE_BUILD_TESTS)
    enable_testing()
    
    # 平台上下文测试
    add_executable(test_platform_context
        tests/platform/test_platform_context.cpp
    )
    
    target_link_libraries(test_platform_context
        PRIVATE Pipeline
    )
    
    add_test(NAME PlatformContextTest COMMAND test_platform_context)
    
    message(STATUS "Tests enabled: test_platform_context")
endif()

# ============================================
# 打印配置信息
# ============================================
message(STATUS "========================================")
message(STATUS "Pipeline Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Examples: ${PIPELINE_BUILD_EXAMPLES}")
message(STATUS "  Build Tests: ${PIPELINE_BUILD_TESTS}")
message(STATUS "========================================")
