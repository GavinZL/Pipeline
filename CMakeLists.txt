cmake_minimum_required(VERSION 3.16)
project(Pipeline VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 选项
option(PIPELINE_BUILD_EXAMPLES "Build examples" OFF)
option(PIPELINE_BUILD_TESTS "Build tests" OFF)

# 核心源文件
set(PIPELINE_CORE_SOURCES
    src/data/FramePacket.cpp
    src/data/FramePort.cpp
    src/entity/ProcessEntity.cpp
    src/entity/GPUEntity.cpp
    src/entity/CPUEntity.cpp
    src/entity/CompositeEntity.cpp
    src/entity/IOEntity.cpp
    src/core/PipelineGraph.cpp
    src/core/PipelineExecutor.cpp
    src/core/PipelineManager.cpp
    src/core/PipelineConfig.cpp
    src/pool/FramePacketPool.cpp
    src/pool/TexturePool.cpp
)

# 示例Entity源文件
set(PIPELINE_ENTITY_SOURCES
    entities/gpu/FilterEntity.cpp
    entities/gpu/BeautyEntity.cpp
    entities/cpu/FaceDetectionEntity.cpp
)

# 合并所有源文件
set(PIPELINE_SOURCES
    ${PIPELINE_CORE_SOURCES}
    ${PIPELINE_ENTITY_SOURCES}
)

# 头文件目录
set(PIPELINE_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/entities
)

# 创建库
add_library(Pipeline STATIC ${PIPELINE_SOURCES})

target_include_directories(Pipeline
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 依赖 - LREngine
if(TARGET LREngine)
    target_link_libraries(Pipeline PUBLIC LREngine)
else()
    # 查找LREngine
    find_path(LRENGINE_INCLUDE_DIR lrengine/core/LRRenderContext.h
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/../LREngine/include
            /usr/local/include
    )
    if(LRENGINE_INCLUDE_DIR)
        target_include_directories(Pipeline PUBLIC ${LRENGINE_INCLUDE_DIR})
    endif()
endif()

# 依赖 - TaskQueue
if(TARGET TaskQueue)
    target_link_libraries(Pipeline PUBLIC TaskQueue)
else()
    find_path(TASKQUEUE_INCLUDE_DIR TaskQueue.h
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/../TaskQueue/TaskQueue
            /usr/local/include
    )
    if(TASKQUEUE_INCLUDE_DIR)
        target_include_directories(Pipeline PUBLIC ${TASKQUEUE_INCLUDE_DIR})
    endif()
endif()

# 编译选项
if(MSVC)
    target_compile_options(Pipeline PRIVATE /W4)
else()
    target_compile_options(Pipeline PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 平台特定
if(ANDROID)
    target_compile_definitions(Pipeline PRIVATE PIPELINE_PLATFORM_ANDROID)
elseif(IOS)
    target_compile_definitions(Pipeline PRIVATE PIPELINE_PLATFORM_IOS)
elseif(APPLE)
    target_compile_definitions(Pipeline PRIVATE PIPELINE_PLATFORM_MACOS)
elseif(WIN32)
    target_compile_definitions(Pipeline PRIVATE PIPELINE_PLATFORM_WINDOWS)
else()
    target_compile_definitions(Pipeline PRIVATE PIPELINE_PLATFORM_LINUX)
endif()

# 安装（仅在作为顶级项目时启用，避免子项目模式下的 export 冲突）
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    install(TARGETS Pipeline
        EXPORT PipelineTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )

    install(DIRECTORY include/pipeline
        DESTINATION include
        FILES_MATCHING PATTERN "*.h"
    )

    # 导出配置
    install(EXPORT PipelineTargets
        FILE PipelineTargets.cmake
        NAMESPACE Pipeline::
        DESTINATION lib/cmake/Pipeline
    )
endif()

# 示例
if(PIPELINE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# 测试
if(PIPELINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
