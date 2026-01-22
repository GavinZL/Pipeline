/**
 * @file CPUEntity.cpp
 * @brief CPUEntity实现 - CPU处理节点基类
 */

#include "pipeline/entity/CPUEntity.h"
#include "pipeline/core/PipelineConfig.h"
#include "pipeline/pool/FramePacketPool.h"

namespace pipeline {

CPUEntity::CPUEntity(const std::string& name)
    : ProcessEntity(name)
{
    // 添加默认输入输出端口
    addInputPort("input");
    addOutputPort("output");
}

CPUEntity::~CPUEntity() = default;

// =============================================================================
// 配置
// =============================================================================

void CPUEntity::setProcessingScale(float scale) {
    mProcessingScale = std::max(0.1f, std::min(1.0f, scale));
}

// =============================================================================
// 执行流程
// =============================================================================

bool CPUEntity::process(const std::vector<FramePacketPtr>& inputs,
                       std::vector<FramePacketPtr>& outputs,
                       PipelineContext& context) {
    if (inputs.empty() || !inputs[0]) {
        return false;
    }
    
    auto input = inputs[0];
    
    // 确保CPU缓冲可用
    if (!ensureCpuBuffer(input)) {
        return false;
    }
    
    const uint8_t* cpuData = input->getCpuBuffer();
    if (!cpuData) {
        return false;
    }
    
    uint32_t width = input->getWidth();
    uint32_t height = input->getHeight();
    uint32_t stride = input->getStride();
    PixelFormat format = input->getFormat();
    
    // 检查是否需要缩放
    const uint8_t* processData = cpuData;
    uint32_t processWidth = width;
    uint32_t processHeight = height;
    uint32_t processStride = stride;
    
    if (mProcessingScale < 1.0f) {
        processWidth = static_cast<uint32_t>(width * mProcessingScale);
        processHeight = static_cast<uint32_t>(height * mProcessingScale);
        
        // 缩放图像
        auto scaledBuffer = scaleImage(cpuData, width, height, 
                                       processWidth, processHeight, format);
        if (scaledBuffer) {
            processData = scaledBuffer.get();
            processStride = processWidth * getPixelFormatBytesPerPixel(format);
            mScaledBuffer = scaledBuffer;
        }
    }
    
    // 准备元数据容器
    std::unordered_map<std::string, std::any> metadata;
    
    // 调用子类实现的CPU处理
    PixelFormat requiredFormat = getRequiredFormat();
    if (requiredFormat != PixelFormat::Unknown && requiredFormat != format) {
        // 需要格式转换（暂不实现）
    }
    
    bool success = processOnCPU(processData, processWidth, processHeight, 
                                processStride, format, metadata);
    
    if (!success) {
        return false;
    }
    
    // 创建输出FramePacket
    FramePacketPtr output;
    
    if (mPassthroughInput) {
        // 透传模式：复用输入的纹理
        output = input->clone();
    } else {
        // 创建新的输出
        auto packetPool = context.getFramePacketPool();
        if (packetPool) {
            output = packetPool->acquire();
        } else {
            output = std::make_shared<FramePacket>(input->getFrameId());
        }
        
        if (!output) {
            return false;
        }
        
        output->setFrameId(input->getFrameId());
        output->setTimestamp(input->getTimestamp());
        output->setSize(input->getWidth(), input->getHeight());
        output->setFormat(input->getFormat());
        
        if (mWriteBackTexture) {
            // 需要将CPU数据写回纹理（暂不实现）
            output->setTexture(input->getTexture());
        } else {
            output->setTexture(input->getTexture());
        }
    }
    
    // 将计算结果添加到元数据
    for (const auto& [key, value] : metadata) {
        output->setMetadata(key, value);
    }
    
    // 调用完成回调
    onProcessComplete(input, output);
    
    outputs.push_back(output);
    return true;
}

// =============================================================================
// 辅助方法
// =============================================================================

bool CPUEntity::ensureCpuBuffer(FramePacketPtr packet) {
    if (!packet) {
        return false;
    }
    
    // 尝试获取CPU缓冲（会触发懒加载）
    const uint8_t* buffer = packet->getCpuBuffer();
    return buffer != nullptr;
}

std::shared_ptr<uint8_t> CPUEntity::scaleImage(const uint8_t* src,
                                               uint32_t srcWidth, uint32_t srcHeight,
                                               uint32_t dstWidth, uint32_t dstHeight,
                                               PixelFormat format) {
    if (!src || srcWidth == 0 || srcHeight == 0 || 
        dstWidth == 0 || dstHeight == 0) {
        return nullptr;
    }
    
    size_t bytesPerPixel = getPixelFormatBytesPerPixel(format);
    if (bytesPerPixel == 0) {
        bytesPerPixel = 4;
    }
    
    size_t dstSize = dstWidth * dstHeight * bytesPerPixel;
    
    // 检查是否可以复用缓冲
    if (mScaledBufferSize < dstSize) {
        uint8_t* buffer = new uint8_t[dstSize];
        mScaledBuffer = std::shared_ptr<uint8_t>(buffer, [](uint8_t* p) { delete[] p; });
        mScaledBufferSize = dstSize;
    }
    
    uint8_t* dst = mScaledBuffer.get();
    
    // 简单的双线性插值缩放
    float xRatio = static_cast<float>(srcWidth) / dstWidth;
    float yRatio = static_cast<float>(srcHeight) / dstHeight;
    
    for (uint32_t y = 0; y < dstHeight; ++y) {
        for (uint32_t x = 0; x < dstWidth; ++x) {
            float srcX = x * xRatio;
            float srcY = y * yRatio;
            
            uint32_t x0 = static_cast<uint32_t>(srcX);
            uint32_t y0 = static_cast<uint32_t>(srcY);
            uint32_t x1 = std::min(x0 + 1, srcWidth - 1);
            uint32_t y1 = std::min(y0 + 1, srcHeight - 1);
            
            float xFrac = srcX - x0;
            float yFrac = srcY - y0;
            
            size_t dstOffset = (y * dstWidth + x) * bytesPerPixel;
            
            for (size_t c = 0; c < bytesPerPixel; ++c) {
                float v00 = src[(y0 * srcWidth + x0) * bytesPerPixel + c];
                float v01 = src[(y0 * srcWidth + x1) * bytesPerPixel + c];
                float v10 = src[(y1 * srcWidth + x0) * bytesPerPixel + c];
                float v11 = src[(y1 * srcWidth + x1) * bytesPerPixel + c];
                
                float v0 = v00 * (1 - xFrac) + v01 * xFrac;
                float v1 = v10 * (1 - xFrac) + v11 * xFrac;
                float v = v0 * (1 - yFrac) + v1 * yFrac;
                
                dst[dstOffset + c] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, v)));
            }
        }
    }
    
    return mScaledBuffer;
}

} // namespace pipeline
