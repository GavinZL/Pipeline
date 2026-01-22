/**
 * @file GPUEntity.cpp
 * @brief GPUEntity实现 - GPU处理节点基类
 */

#include "pipeline/entity/GPUEntity.h"
#include "pipeline/core/PipelineConfig.h"
#include "pipeline/pool/TexturePool.h"
#include "pipeline/pool/FramePacketPool.h"

// LREngine头文件（实际使用时需要包含）
 #include "lrengine/core/LRRenderContext.h"
 #include "lrengine/core/LRTexture.h"
 #include "lrengine/core/LRFrameBuffer.h"
 #include "lrengine/core/LRPipelineState.h"

namespace pipeline {

// 默认顶点着色器
const char* GPUEntity::sDefaultVertexShader = R"(
#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

// 默认片段着色器（直通）
const char* GPUEntity::sDefaultFragmentShader = R"(
#version 300 es
precision mediump float;

in vec2 vTexCoord;
out vec4 fragColor;

uniform sampler2D uTexture;

void main() {
    fragColor = texture(uTexture, vTexCoord);
}
)";

GPUEntity::GPUEntity(const std::string& name)
    : ProcessEntity(name)
{
    // 添加默认输入输出端口
    addInputPort("input");
    addOutputPort("output");
    
    // 设置默认着色器源码
    mVertexShaderSource = sDefaultVertexShader;
    mFragmentShaderSource = sDefaultFragmentShader;
}

GPUEntity::~GPUEntity() = default;

// =============================================================================
// 渲染上下文
// =============================================================================

void GPUEntity::setRenderContext(lrengine::render::LRRenderContext* context) {
    mRenderContext = context;
    mShaderNeedsRebuild = true;
}

// =============================================================================
// 着色器管理
// =============================================================================

void GPUEntity::setVertexShaderSource(const std::string& source) {
    mVertexShaderSource = source;
    mShaderNeedsRebuild = true;
}

void GPUEntity::setFragmentShaderSource(const std::string& source) {
    mFragmentShaderSource = source;
    mShaderNeedsRebuild = true;
}

bool GPUEntity::loadShaderFromFile(const std::string& vertexPath, 
                                   const std::string& fragmentPath) {
    // 从文件读取着色器源码
    // 这里需要实现文件读取逻辑
    // 暂时返回false
    return false;
}

// =============================================================================
// 输出配置
// =============================================================================

void GPUEntity::setOutputSize(uint32_t width, uint32_t height) {
    mOutputWidth = width;
    mOutputHeight = height;
}

void GPUEntity::setOutputFormat(PixelFormat format) {
    mOutputFormat = format;
}

// =============================================================================
// 执行流程
// =============================================================================

bool GPUEntity::prepare(PipelineContext& context) {
    if (!mRenderContext) {
        mRenderContext = context.getRenderContext();
    }
    
    if (!mRenderContext) {
        return false;
    }
    
    // 确保着色器已创建
    if (mShaderNeedsRebuild || !mShaderProgram) {
        if (!setupShader()) {
            return false;
        }
        mShaderNeedsRebuild = false;
    }
    
    // 确保全屏顶点缓冲已创建
    if (!mFullscreenQuad) {
        if (!createFullscreenQuad()) {
            return false;
        }
    }
    
    return true;
}

bool GPUEntity::process(const std::vector<FramePacketPtr>& inputs,
                       std::vector<FramePacketPtr>& outputs,
                       PipelineContext& context) {
    if (inputs.empty() || !inputs[0]) {
        return false;
    }
    
    auto input = inputs[0];
    
    // 确定输出尺寸
    uint32_t outWidth = mOutputWidth > 0 ? mOutputWidth : input->getWidth();
    uint32_t outHeight = mOutputHeight > 0 ? mOutputHeight : input->getHeight();
    
    // 确保FrameBuffer已创建
    if (!ensureFrameBuffer(outWidth, outHeight)) {
        return false;
    }
    
    // 创建输出FramePacket
    auto texturePool = context.getTexturePool();
    auto packetPool = context.getFramePacketPool();
    
    FramePacketPtr output;
    if (packetPool) {
        output = packetPool->acquire();
    } else {
        output = std::make_shared<FramePacket>(input->getFrameId());
    }
    
    if (!output) {
        return false;
    }
    
    // 复制基本信息
    output->setFrameId(input->getFrameId());
    output->setTimestamp(input->getTimestamp());
    output->setSize(outWidth, outHeight);
    output->setFormat(mOutputFormat);
    
    // 执行GPU处理
    if (!processGPU(inputs, output)) {
        return false;
    }
    
    // 设置输出纹理
    output->setTexture(mOutputTexture);
    
    outputs.push_back(output);
    return true;
}

void GPUEntity::finalize(PipelineContext& context) {
    // 清理临时资源（如果需要）
}

// =============================================================================
// 子类实现
// =============================================================================

bool GPUEntity::setupShader() {
    if (!mRenderContext) {
        return false;
    }
    
    // 使用LREngine创建着色器程序
    // 这里需要根据LREngine的实际API来实现
    /*
    // 创建顶点着色器
    lrengine::render::ShaderDescriptor vsDesc;
    vsDesc.stage = lrengine::render::ShaderStage::Vertex;
    vsDesc.source = mVertexShaderSource.c_str();
    auto vs = mRenderContext->CreateShader(vsDesc);
    
    // 创建片段着色器
    lrengine::render::ShaderDescriptor fsDesc;
    fsDesc.stage = lrengine::render::ShaderStage::Fragment;
    fsDesc.source = mFragmentShaderSource.c_str();
    auto fs = mRenderContext->CreateShader(fsDesc);
    
    // 创建着色器程序
    mShaderProgram = std::shared_ptr<lrengine::render::LRShaderProgram>(
        mRenderContext->CreateShaderProgram(vs, fs));
    */
    
    return true;
}

bool GPUEntity::processGPU(const std::vector<FramePacketPtr>& inputs, 
                          FramePacketPtr output) {
    if (!mRenderContext || !mShaderProgram || !mFrameBuffer) {
        return false;
    }
    
    // 开始渲染到FBO
    // mRenderContext->BeginRenderPass(mFrameBuffer.get());
    
    // 设置视口
    // mRenderContext->SetViewport(0, 0, output->getWidth(), output->getHeight());
    
    // 绑定着色器
    // mRenderContext->SetPipelineState(mPipelineState.get());
    
    // 绑定输入纹理
    bindInputTextures(inputs, 0);
    
    // 设置uniform参数
    if (!inputs.empty() && inputs[0]) {
        setUniforms(inputs[0].get());
    }
    
    // 绘制全屏四边形
    drawFullscreenQuad();
    
    // 解绑纹理
    unbindInputTextures(inputs.size(), 0);
    
    // 结束渲染
    // mRenderContext->EndRenderPass();
    
    return true;
}

bool GPUEntity::ensureFrameBuffer(uint32_t width, uint32_t height) {
    if (!mRenderContext) {
        return false;
    }
    
    // 检查是否需要重建
    if (mOutputTexture && mFrameBuffer) {
        // 这里需要检查尺寸是否匹配
        // 暂时假设需要重建
    }
    
    // 创建输出纹理
    // 使用LREngine创建纹理
    /*
    lrengine::render::TextureDescriptor texDesc;
    texDesc.width = width;
    texDesc.height = height;
    texDesc.format = convertPixelFormat(mOutputFormat);
    texDesc.mipLevels = 1;
    
    mOutputTexture = std::shared_ptr<lrengine::render::LRTexture>(
        mRenderContext->CreateTexture(texDesc));
    
    // 创建FrameBuffer
    lrengine::render::FrameBufferDescriptor fbDesc;
    fbDesc.width = width;
    fbDesc.height = height;
    
    mFrameBuffer = std::shared_ptr<lrengine::render::LRFrameBuffer>(
        mRenderContext->CreateFrameBuffer(fbDesc));
    
    // 附加颜色纹理
    mFrameBuffer->AttachColorTexture(mOutputTexture.get(), 0);
    */
    
    return true;
}

bool GPUEntity::createFullscreenQuad() {
    if (!mRenderContext) {
        return false;
    }
    
    // 全屏四边形顶点数据
    // Position (x, y), TexCoord (u, v)
    static const float vertices[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,  // 左下
         1.0f, -1.0f,  1.0f, 0.0f,  // 右下
        -1.0f,  1.0f,  0.0f, 1.0f,  // 左上
         1.0f,  1.0f,  1.0f, 1.0f,  // 右上
    };
    
    // 使用LREngine创建顶点缓冲
    /*
    lrengine::render::BufferDescriptor bufDesc;
    bufDesc.size = sizeof(vertices);
    bufDesc.usage = lrengine::render::BufferUsage::Static;
    bufDesc.type = lrengine::render::BufferType::Vertex;
    bufDesc.data = vertices;
    
    mFullscreenQuad = std::shared_ptr<lrengine::render::LRVertexBuffer>(
        mRenderContext->CreateVertexBuffer(bufDesc));
    */
    
    return true;
}

void GPUEntity::drawFullscreenQuad() {
    if (!mFullscreenQuad) {
        return;
    }
    
    // 绑定顶点缓冲
    // mRenderContext->SetVertexBuffer(mFullscreenQuad.get(), 0);
    
    // 绘制三角形条带（4个顶点）
    // mRenderContext->Draw(0, 4);
}

void GPUEntity::bindInputTextures(const std::vector<FramePacketPtr>& inputs, 
                                 uint32_t startSlot) {
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (inputs[i] && inputs[i]->getTexture()) {
            // mRenderContext->SetTexture(inputs[i]->getTexture().get(), startSlot + i);
        }
    }
}

void GPUEntity::unbindInputTextures(size_t count, uint32_t startSlot) {
    for (size_t i = 0; i < count; ++i) {
        // mRenderContext->SetTexture(nullptr, startSlot + i);
    }
}

} // namespace pipeline
