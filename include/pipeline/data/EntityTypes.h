/**
 * @file EntityTypes.h
 * @brief Pipeline核心类型定义
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <functional>

namespace pipeline {

// =============================================================================
// 类型别名
// =============================================================================

using EntityId = int32_t;
constexpr EntityId InvalidEntityId = -1;

// =============================================================================
// Entity状态枚举
// =============================================================================

/**
 * @brief Entity执行状态
 */
enum class EntityState : uint8_t {
    Idle,        // 空闲状态，等待输入
    Ready,       // 输入就绪，可以执行
    Processing,  // 正在执行
    Completed,   // 执行完成
    Blocked,     // 等待依赖
    Error        // 错误状态
};

/**
 * @brief Entity类型
 */
enum class EntityType : uint8_t {
    Unknown,
    Input,       // 输入节点（相机数据源）
    Output,      // 输出节点（显示/编码）
    GPU,         // GPU处理节点
    CPU,         // CPU处理节点
    Composite    // 合成节点（多输入）
};

/**
 * @brief 像素格式
 */
enum class PixelFormat : uint8_t {
    Unknown,
    RGBA8,       // 8位RGBA
    BGRA8,       // 8位BGRA
    RGB8,        // 8位RGB
    YUV420,      // YUV420平面格式
    NV12,        // NV12格式（Y + UV交织）
    NV21,        // NV21格式（Y + VU交织）
    RGBA16F,     // 16位浮点RGBA
    RGBA32F,     // 32位浮点RGBA
    R8,          // 单通道8位
    RG8,         // 双通道8位
    OES          // 外部纹理（Android OES）
};

/**
 * @brief 执行队列类型
 */
enum class ExecutionQueue : uint8_t {
    GPU,         // GPU串行队列
    CPUParallel, // CPU并行队列
    IO           // IO队列
};

// =============================================================================
// 连接信息
// =============================================================================

/**
 * @brief 端口连接信息
 */
struct Connection {
    EntityId srcEntity = InvalidEntityId;
    std::string srcPort;
    EntityId dstEntity = InvalidEntityId;
    std::string dstPort;
};

// =============================================================================
// 前向声明
// =============================================================================

class FramePacket;
class ProcessEntity;
class InputPort;
class OutputPort;
class PipelineContext;
class PipelineGraph;
class PipelineExecutor;
class PipelineManager;
class TexturePool;
class FramePacketPool;

// =============================================================================
// 智能指针别名
// =============================================================================

using FramePacketPtr = std::shared_ptr<FramePacket>;
using ProcessEntityPtr = std::shared_ptr<ProcessEntity>;
using PipelineContextPtr = std::shared_ptr<PipelineContext>;
using TexturePoolPtr = std::shared_ptr<TexturePool>;
using FramePacketPoolPtr = std::shared_ptr<FramePacketPool>;

// =============================================================================
// 回调类型
// =============================================================================

using EntityCallback = std::function<void(EntityId, EntityState)>;
using FrameCallback = std::function<void(FramePacketPtr)>;
using ErrorCallback = std::function<void(EntityId, const std::string&)>;

// =============================================================================
// 辅助函数
// =============================================================================

inline const char* entityStateToString(EntityState state) {
    switch (state) {
        case EntityState::Idle: return "Idle";
        case EntityState::Ready: return "Ready";
        case EntityState::Processing: return "Processing";
        case EntityState::Completed: return "Completed";
        case EntityState::Blocked: return "Blocked";
        case EntityState::Error: return "Error";
        default: return "Unknown";
    }
}

inline const char* entityTypeToString(EntityType type) {
    switch (type) {
        case EntityType::Input: return "Input";
        case EntityType::Output: return "Output";
        case EntityType::GPU: return "GPU";
        case EntityType::CPU: return "CPU";
        case EntityType::Composite: return "Composite";
        default: return "Unknown";
    }
}

inline size_t getPixelFormatBytesPerPixel(PixelFormat format) {
    switch (format) {
        case PixelFormat::RGBA8:
        case PixelFormat::BGRA8:
            return 4;
        case PixelFormat::RGB8:
            return 3;
        case PixelFormat::RG8:
            return 2;
        case PixelFormat::R8:
            return 1;
        case PixelFormat::RGBA16F:
            return 8;
        case PixelFormat::RGBA32F:
            return 16;
        default:
            return 0;
    }
}

} // namespace pipeline
