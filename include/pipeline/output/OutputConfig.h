/**
 * @file OutputConfig.h
 * @brief Pipeline 输出配置定义
 */

#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <memory>

// 前向声明
namespace lrengine {
namespace render {
class LRTexture;
class LRPlanarTexture;
} // namespace render
} // namespace lrengine

namespace pipeline {
namespace output {

// =============================================================================
// 输出格式
// =============================================================================

/**
 * @brief 输出数据格式
 */
enum class OutputFormat : uint8_t {
    RGBA,       ///< RGBA 像素数据
    BGRA,       ///< BGRA 像素数据（iOS 常用）
    RGB,        ///< RGB 像素数据
    YUV420,     ///< YUV420 平面数据
    NV12,       ///< NV12 半平面数据
    NV21,       ///< NV21 半平面数据（Android 常用）
    Texture     ///< GPU 纹理
};

/**
 * @brief 输出目标类型
 */
enum class OutputTargetType : uint8_t {
    Display,    ///< 屏幕显示
    Encoder,    ///< 视频编码器
    Callback,   ///< 回调函数
    File,       ///< 文件写入
    Stream,     ///< 网络推流
    Custom      ///< 自定义目标
};

/**
 * @brief 输出数据类型
 */
enum class OutputDataType : uint8_t {
    CPUBuffer,  ///< CPU 缓冲区
    GPUTexture, ///< GPU 纹理
    Both        ///< 同时提供两种
};

// =============================================================================
// 输出回调
// =============================================================================

/**
 * @brief CPU 数据回调
 */
using CPUOutputCallback = std::function<void(
    const uint8_t* data,    ///< 数据指针
    size_t dataSize,        ///< 数据大小
    uint32_t width,         ///< 宽度
    uint32_t height,        ///< 高度
    OutputFormat format,    ///< 格式
    int64_t timestamp       ///< 时间戳
)>;

/**
 * @brief GPU 纹理回调
 */
using GPUOutputCallback = std::function<void(
    uint32_t textureId,     ///< OpenGL 纹理 ID
    void* metalTexture,     ///< Metal 纹理（iOS）
    uint32_t width,         ///< 宽度
    uint32_t height,        ///< 高度
    int64_t timestamp       ///< 时间戳
)>;

// =============================================================================
// 显示配置
// =============================================================================

/**
 * @brief 显示填充模式
 */
enum class DisplayFillMode : uint8_t {
    AspectFit,      ///< 保持比例适应（可能有黑边）
    AspectFill,     ///< 保持比例填充（可能裁剪）
    Stretch         ///< 拉伸填充
};

/**
 * @brief 显示旋转角度
 */
enum class DisplayRotation : uint16_t {
    None = 0,       ///< 不旋转
    CW90 = 90,      ///< 顺时针 90 度
    CW180 = 180,    ///< 顺时针 180 度
    CW270 = 270     ///< 顺时针 270 度
};

/**
 * @brief 显示配置
 */
struct DisplayConfig {
    DisplayFillMode fillMode = DisplayFillMode::AspectFit;
    DisplayRotation rotation = DisplayRotation::None;
    bool flipHorizontal = false;    ///< 水平翻转
    bool flipVertical = false;      ///< 垂直翻转
    float backgroundColor[4] = {0.0f, 0.0f, 0.0f, 1.0f}; ///< 背景色 RGBA
};

// =============================================================================
// 编码器配置
// =============================================================================

/**
 * @brief 编码器类型
 */
enum class EncoderType : uint8_t {
    H264,       ///< H.264/AVC
    H265,       ///< H.265/HEVC
    VP8,        ///< VP8
    VP9,        ///< VP9
    AV1         ///< AV1
};

/**
 * @brief 编码配置
 */
struct EncoderConfig {
    EncoderType type = EncoderType::H264;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bitrate = 4000000;     ///< 比特率（bps）
    uint32_t frameRate = 30;        ///< 帧率
    uint32_t keyFrameInterval = 2;  ///< 关键帧间隔（秒）
    bool useHardwareEncoder = true; ///< 使用硬件编码
    std::string profile;            ///< 编码 profile
    std::string level;              ///< 编码 level
};

// =============================================================================
// 输出配置
// =============================================================================

/**
 * @brief 输出目标配置
 */
struct OutputTargetConfig {
    std::string name;                           ///< 目标名称
    OutputTargetType type = OutputTargetType::Display;
    OutputFormat format = OutputFormat::RGBA;
    OutputDataType dataType = OutputDataType::GPUTexture;
    bool enabled = true;                        ///< 是否启用
    
    // 显示配置（type == Display）
    DisplayConfig display;
    
    // 编码配置（type == Encoder）
    EncoderConfig encoder;
    
    // 回调（type == Callback）
    CPUOutputCallback cpuCallback;
    GPUOutputCallback gpuCallback;
    
    // 文件路径（type == File）
    std::string filePath;
};

/**
 * @brief 输出实体配置
 */
struct OutputConfig {
    std::vector<OutputTargetConfig> targets;    ///< 输出目标列表
    bool enableMultiTarget = false;             ///< 启用多目标输出
    bool asyncOutput = true;                    ///< 异步输出
    size_t outputQueueSize = 3;                 ///< 输出队列大小
};

// =============================================================================
// 输出数据包
// =============================================================================

/**
 * @brief 输出数据包
 */
struct OutputData {
    // CPU 数据
    const uint8_t* cpuData = nullptr;
    size_t cpuDataSize = 0;
    
    // GPU 数据（原始句柄）
    uint32_t textureId = 0;
    void* metalTexture = nullptr;
    
    // GPU 数据（统一使用多平面纹理，RGBA 作为单平面）
    std::shared_ptr<lrengine::render::LRPlanarTexture> planarTexture;
    
    // 元信息
    uint32_t width = 0;
    uint32_t height = 0;
    OutputFormat format = OutputFormat::RGBA;
    int64_t timestamp = 0;
    uint64_t frameId = 0;
    
    // 便捷方法
    bool hasGpuData() const {
        return planarTexture || textureId != 0 || metalTexture != nullptr;
    }
    
    bool hasCpuData() const {
        return cpuData != nullptr && cpuDataSize > 0;
    }
};

} // namespace output
} // namespace pipeline
