/**
 * @file InputFormat.h
 * @brief Pipeline 输入格式定义
 */

#pragma once

#include <cstdint>

namespace pipeline {
namespace input {

/**
 * @brief 输入数据格式
 */
enum class InputFormat : uint8_t {
    RGBA,       // RGBA 像素数据
    BGRA,       // BGRA 像素数据（iOS 常用）
    RGB,        // RGB 像素数据
    YUV420,     // YUV420 平面数据
    NV12,       // NV12 半平面数据
    NV21,       // NV21 半平面数据（Android 常用）
    OES,        // Android OES 纹理
    Texture     // 已有 GPU 纹理
};

/**
 * @brief 输入数据类型
 */
enum class InputDataType : uint8_t {
    CPUBuffer,      // CPU 缓冲区数据
    GPUTexture,     // GPU 纹理数据
    Both,           // 双路数据（同时提供 CPU 和 GPU）
    PlatformBuffer  // 平台特定缓冲区（如 CVPixelBuffer、HardwareBuffer）
};

/**
 * @brief 输入源类型
 */
enum class InputSourceType : uint8_t {
    Camera,     // 相机输入
    Video,      // 视频文件
    Image,      // 静态图像
    Stream,     // 网络流
    Custom      // 自定义输入
};

/**
 * @brief 输入配置
 */
struct InputConfig {
    InputFormat format = InputFormat::RGBA;
    InputDataType dataType = InputDataType::CPUBuffer;
    InputSourceType sourceType = InputSourceType::Camera;
    uint32_t width = 0;
    uint32_t height = 0;
    bool enableDualOutput = false;  // 是否启用双路输出
};

} // namespace input
} // namespace pipeline
