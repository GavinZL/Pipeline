/**
 * @file CompositeEntity.cpp
 * @brief 合成节点实现 - 多输入混合处理
 */

#include "pipeline/entity/CompositeEntity.h"
#include "pipeline/data/FramePacket.h"
#include "pipeline/data/FramePort.h"
#include "pipeline/pool/TexturePool.h"
#include "pipeline/core/PipelineConfig.h"

#include "lrengine/core/LRTexture.h"
#include <algorithm>
#include <sstream>
#include <cmath>

namespace pipeline {

// =============================================================================
// 着色器源码
// =============================================================================

namespace {

const char* kCompositeVertexShader = R"(
attribute vec4 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;

void main() {
    gl_Position = aPosition;
    vTexCoord = aTexCoord;
}
)";

// 基础混合着色器（支持两路输入）
const char* kCompositeFragmentShaderTemplate = R"(
precision mediump float;
varying vec2 vTexCoord;

uniform sampler2D uTexture0;
uniform sampler2D uTexture1;
uniform float uAlpha0;
uniform float uAlpha1;
uniform vec4 uUVTransform0;  // x, y, width, height
uniform vec4 uUVTransform1;
uniform int uBlendMode;
uniform int uLayout;
uniform vec4 uPipRect;  // x, y, width, height for PiP

// 混合函数
vec3 blendNormal(vec3 base, vec3 blend, float opacity) {
    return mix(base, blend, opacity);
}

vec3 blendAdd(vec3 base, vec3 blend, float opacity) {
    return mix(base, min(base + blend, vec3(1.0)), opacity);
}

vec3 blendMultiply(vec3 base, vec3 blend, float opacity) {
    return mix(base, base * blend, opacity);
}

vec3 blendScreen(vec3 base, vec3 blend, float opacity) {
    return mix(base, vec3(1.0) - (vec3(1.0) - base) * (vec3(1.0) - blend), opacity);
}

vec3 blendOverlay(vec3 base, vec3 blend, float opacity) {
    vec3 result;
    result.r = base.r < 0.5 ? (2.0 * base.r * blend.r) : (1.0 - 2.0 * (1.0 - base.r) * (1.0 - blend.r));
    result.g = base.g < 0.5 ? (2.0 * base.g * blend.g) : (1.0 - 2.0 * (1.0 - base.g) * (1.0 - blend.g));
    result.b = base.b < 0.5 ? (2.0 * base.b * blend.b) : (1.0 - 2.0 * (1.0 - base.b) * (1.0 - blend.b));
    return mix(base, result, opacity);
}

vec3 blendSoftLight(vec3 base, vec3 blend, float opacity) {
    vec3 result = (1.0 - 2.0 * blend) * base * base + 2.0 * blend * base;
    return mix(base, result, opacity);
}

vec3 blendHardLight(vec3 base, vec3 blend, float opacity) {
    vec3 result;
    result.r = blend.r < 0.5 ? (2.0 * base.r * blend.r) : (1.0 - 2.0 * (1.0 - base.r) * (1.0 - blend.r));
    result.g = blend.g < 0.5 ? (2.0 * base.g * blend.g) : (1.0 - 2.0 * (1.0 - base.g) * (1.0 - blend.g));
    result.b = blend.b < 0.5 ? (2.0 * base.b * blend.b) : (1.0 - 2.0 * (1.0 - base.b) * (1.0 - blend.b));
    return mix(base, result, opacity);
}

vec3 blendDifference(vec3 base, vec3 blend, float opacity) {
    return mix(base, abs(base - blend), opacity);
}

vec3 blendExclusion(vec3 base, vec3 blend, float opacity) {
    return mix(base, base + blend - 2.0 * base * blend, opacity);
}

vec3 applyBlend(vec3 base, vec3 blend, float opacity) {
    if (uBlendMode == 0) return blendNormal(base, blend, opacity);       // Normal
    else if (uBlendMode == 1) return blendAdd(base, blend, opacity);     // Add
    else if (uBlendMode == 2) return blendMultiply(base, blend, opacity);// Multiply
    else if (uBlendMode == 3) return blendScreen(base, blend, opacity);  // Screen
    else if (uBlendMode == 4) return blendOverlay(base, blend, opacity); // Overlay
    else if (uBlendMode == 5) return blendSoftLight(base, blend, opacity);// SoftLight
    else if (uBlendMode == 6) return blendHardLight(base, blend, opacity);// HardLight
    else if (uBlendMode == 7) return blendDifference(base, blend, opacity);// Difference
    else if (uBlendMode == 8) return blendExclusion(base, blend, opacity);// Exclusion
    return blendNormal(base, blend, opacity);
}

vec2 transformUV(vec2 uv, vec4 transform) {
    return transform.xy + uv * transform.zw;
}

void main() {
    vec2 uv = vTexCoord;
    vec4 color0 = vec4(0.0);
    vec4 color1 = vec4(0.0);
    
    // 根据布局模式采样
    if (uLayout == 0) {
        // Blend模式：全屏混合
        color0 = texture2D(uTexture0, transformUV(uv, uUVTransform0));
        color1 = texture2D(uTexture1, transformUV(uv, uUVTransform1));
        color0.a *= uAlpha0;
        color1.a *= uAlpha1;
    }
    else if (uLayout == 1) {
        // SplitHorizontal：水平分屏
        if (uv.x < 0.5) {
            color0 = texture2D(uTexture0, vec2(uv.x * 2.0, uv.y));
            color0.a *= uAlpha0;
        } else {
            color1 = texture2D(uTexture1, vec2((uv.x - 0.5) * 2.0, uv.y));
            color1.a *= uAlpha1;
            gl_FragColor = color1;
            return;
        }
        gl_FragColor = color0;
        return;
    }
    else if (uLayout == 2) {
        // SplitVertical：垂直分屏
        if (uv.y < 0.5) {
            color0 = texture2D(uTexture0, vec2(uv.x, uv.y * 2.0));
            color0.a *= uAlpha0;
        } else {
            color1 = texture2D(uTexture1, vec2(uv.x, (uv.y - 0.5) * 2.0));
            color1.a *= uAlpha1;
            gl_FragColor = color1;
            return;
        }
        gl_FragColor = color0;
        return;
    }
    else if (uLayout == 3) {
        // Grid2x2：四宫格
        vec2 quadUV;
        int quadrant = 0;
        if (uv.x < 0.5 && uv.y < 0.5) {
            quadUV = uv * 2.0;
            quadrant = 0;
        } else if (uv.x >= 0.5 && uv.y < 0.5) {
            quadUV = vec2((uv.x - 0.5) * 2.0, uv.y * 2.0);
            quadrant = 1;
        } else if (uv.x < 0.5 && uv.y >= 0.5) {
            quadUV = vec2(uv.x * 2.0, (uv.y - 0.5) * 2.0);
            quadrant = 2;
        } else {
            quadUV = vec2((uv.x - 0.5) * 2.0, (uv.y - 0.5) * 2.0);
            quadrant = 3;
        }
        // 简化：只使用两路输入交替填充
        if (quadrant == 0 || quadrant == 3) {
            color0 = texture2D(uTexture0, quadUV);
            color0.a *= uAlpha0;
            gl_FragColor = color0;
        } else {
            color1 = texture2D(uTexture1, quadUV);
            color1.a *= uAlpha1;
            gl_FragColor = color1;
        }
        return;
    }
    else if (uLayout == 4) {
        // PictureInPicture：画中画
        color0 = texture2D(uTexture0, uv);
        color0.a *= uAlpha0;
        
        // 检查是否在小窗口区域内
        vec2 pipMin = uPipRect.xy;
        vec2 pipMax = uPipRect.xy + uPipRect.zw;
        if (uv.x >= pipMin.x && uv.x <= pipMax.x && uv.y >= pipMin.y && uv.y <= pipMax.y) {
            vec2 pipUV = (uv - pipMin) / uPipRect.zw;
            color1 = texture2D(uTexture1, pipUV);
            color1.a *= uAlpha1;
            // 小窗口直接覆盖
            gl_FragColor = mix(color0, color1, color1.a);
            return;
        }
        gl_FragColor = color0;
        return;
    }
    
    // Blend模式应用混合
    vec3 blended = applyBlend(color0.rgb, color1.rgb, color1.a * uAlpha1);
    float alpha = color0.a + color1.a * (1.0 - color0.a);
    gl_FragColor = vec4(blended, alpha);
}
)";

} // anonymous namespace

// =============================================================================
// CompositeEntity 实现
// =============================================================================

CompositeEntity::CompositeEntity(const std::string& name, size_t inputCount)
    : GPUEntity(name) {
    // 初始化输入配置
    mInputConfigs.resize(inputCount);
    
    // 创建多个输入端口
    for (size_t i = 0; i < inputCount; ++i) {
        addInputPort("input" + std::to_string(i));
    }
    
    // 创建输出端口
    addOutputPort("output");
    
    // 设置执行队列
//    setExecutionQueue(ExecutionQueue::GPU);
}

CompositeEntity::~CompositeEntity() = default;

// =============================================================================
// 混合配置
// =============================================================================

void CompositeEntity::setBlendMode(BlendMode mode) {
    mBlendMode = mode;
    mNeedsShaderUpdate = true;
}

void CompositeEntity::setLayout(CompositeLayout layout) {
    mLayout = layout;
    calculateUVTransforms();
    mNeedsShaderUpdate = true;
}

void CompositeEntity::setPipConfig(const PipConfig& config) {
    mPipConfig = config;
}

// =============================================================================
// 输入配置
// =============================================================================

void CompositeEntity::setInputAlpha(size_t inputIndex, float alpha) {
    if (inputIndex < mInputConfigs.size()) {
        mInputConfigs[inputIndex].alpha = std::clamp(alpha, 0.0f, 1.0f);
    }
}

float CompositeEntity::getInputAlpha(size_t inputIndex) const {
    if (inputIndex < mInputConfigs.size()) {
        return mInputConfigs[inputIndex].alpha;
    }
    return 1.0f;
}

void CompositeEntity::setInputTransform(size_t inputIndex, const float* transform) {
    if (inputIndex < mInputConfigs.size() && transform != nullptr) {
        std::memcpy(mInputConfigs[inputIndex].transform, transform, 16 * sizeof(float));
    }
}

void CompositeEntity::setInputVisible(size_t inputIndex, bool visible) {
    if (inputIndex < mInputConfigs.size()) {
        mInputConfigs[inputIndex].visible = visible;
    }
}

bool CompositeEntity::isInputVisible(size_t inputIndex) const {
    if (inputIndex < mInputConfigs.size()) {
        return mInputConfigs[inputIndex].visible;
    }
    return false;
}

void CompositeEntity::setInputZOrder(size_t inputIndex, int32_t zOrder) {
    if (inputIndex < mInputConfigs.size()) {
        mInputConfigs[inputIndex].zOrder = zOrder;
    }
}

// =============================================================================
// 输入管理
// =============================================================================

size_t CompositeEntity::addInput() {
    size_t index = mInputConfigs.size();
    mInputConfigs.push_back(InputConfig{});
    addInputPort("input" + std::to_string(index));
    return index;
}

// =============================================================================
// Shader设置
// =============================================================================

bool CompositeEntity::setupShader() {
    // 生成着色器源码
    std::string fragmentSource = generateBlendShader();
    
    // 设置顶点着色器
    mVertexShaderSource = kCompositeVertexShader;
    mFragmentShaderSource = fragmentSource;
    
    // TODO: 使用LREngine创建着色器程序
    // mShaderProgram = mRenderContext->createShaderProgram(
    //     mVertexShaderSource, mFragmentShaderSource);
    
    // 缓存Uniform位置
    // mBlendModeLocation = mShaderProgram->getUniformLocation("uBlendMode");
    // mInputCountLocation = mShaderProgram->getUniformLocation("uInputCount");
    
    // 缓存每个输入的Uniform位置
    mInputAlphaLocations.resize(mInputConfigs.size());
    mInputUVTransformLocations.resize(mInputConfigs.size());
    
    for (size_t i = 0; i < mInputConfigs.size(); ++i) {
        // mInputAlphaLocations[i] = mShaderProgram->getUniformLocation(
        //     "uAlpha" + std::to_string(i));
        // mInputUVTransformLocations[i] = mShaderProgram->getUniformLocation(
        //     "uUVTransform" + std::to_string(i));
    }
    
    return true;
}

void CompositeEntity::setUniforms(FramePacket* input) {
    if (!mShaderProgram) return;
    
    // TODO: 设置Uniforms
    // mShaderProgram->setUniform(mBlendModeLocation, static_cast<int>(mBlendMode));
    // mShaderProgram->setUniform("uLayout", static_cast<int>(mLayout));
    
    // 设置画中画参数
    // mShaderProgram->setUniform("uPipRect", 
    //     mPipConfig.x, mPipConfig.y, mPipConfig.width, mPipConfig.height);
}

std::string CompositeEntity::generateBlendShader() const {
    // 目前使用模板着色器
    // 未来可根据输入数量和混合模式生成优化的着色器
    return kCompositeFragmentShaderTemplate;
}

void CompositeEntity::calculateUVTransforms() {
    // 根据布局计算每个输入的UV变换
    switch (mLayout) {
        case CompositeLayout::Blend:
            // 全屏，不需要变换
            for (auto& config : mInputConfigs) {
                config.uvTransform[0] = 0.0f;
                config.uvTransform[1] = 0.0f;
                config.uvTransform[2] = 1.0f;
                config.uvTransform[3] = 1.0f;
            }
            break;
            
        case CompositeLayout::SplitHorizontal:
            // 水平分屏
            if (mInputConfigs.size() >= 2) {
                mInputConfigs[0].uvTransform[0] = 0.0f;
                mInputConfigs[0].uvTransform[1] = 0.0f;
                mInputConfigs[0].uvTransform[2] = 0.5f;
                mInputConfigs[0].uvTransform[3] = 1.0f;
                
                mInputConfigs[1].uvTransform[0] = 0.5f;
                mInputConfigs[1].uvTransform[1] = 0.0f;
                mInputConfigs[1].uvTransform[2] = 0.5f;
                mInputConfigs[1].uvTransform[3] = 1.0f;
            }
            break;
            
        case CompositeLayout::SplitVertical:
            // 垂直分屏
            if (mInputConfigs.size() >= 2) {
                mInputConfigs[0].uvTransform[0] = 0.0f;
                mInputConfigs[0].uvTransform[1] = 0.0f;
                mInputConfigs[0].uvTransform[2] = 1.0f;
                mInputConfigs[0].uvTransform[3] = 0.5f;
                
                mInputConfigs[1].uvTransform[0] = 0.0f;
                mInputConfigs[1].uvTransform[1] = 0.5f;
                mInputConfigs[1].uvTransform[2] = 1.0f;
                mInputConfigs[1].uvTransform[3] = 0.5f;
            }
            break;
            
        case CompositeLayout::Grid2x2:
            // 2x2网格
            for (size_t i = 0; i < std::min(mInputConfigs.size(), size_t(4)); ++i) {
                mInputConfigs[i].uvTransform[0] = (i % 2) * 0.5f;
                mInputConfigs[i].uvTransform[1] = (i / 2) * 0.5f;
                mInputConfigs[i].uvTransform[2] = 0.5f;
                mInputConfigs[i].uvTransform[3] = 0.5f;
            }
            break;
            
        case CompositeLayout::PictureInPicture:
            // 画中画：主画面全屏，副画面小窗
            if (mInputConfigs.size() >= 2) {
                mInputConfigs[0].uvTransform[0] = 0.0f;
                mInputConfigs[0].uvTransform[1] = 0.0f;
                mInputConfigs[0].uvTransform[2] = 1.0f;
                mInputConfigs[0].uvTransform[3] = 1.0f;
                
                // 小窗使用PipConfig
                mInputConfigs[1].uvTransform[0] = mPipConfig.x;
                mInputConfigs[1].uvTransform[1] = mPipConfig.y;
                mInputConfigs[1].uvTransform[2] = mPipConfig.width;
                mInputConfigs[1].uvTransform[3] = mPipConfig.height;
            }
            break;
    }
}

// =============================================================================
// GPU处理
// =============================================================================

bool CompositeEntity::processGPU(const std::vector<FramePacketPtr>& inputs,
                                 FramePacketPtr output) {
    if (inputs.empty() || !output) {
        return false;
    }
    
    // 检查输入是否满足要求
    if (mRequireAllInputs && inputs.size() < mInputConfigs.size()) {
        return false;
    }
    
    // 确保着色器已创建
    if (!mShaderProgram || mNeedsShaderUpdate) {
        if (!setupShader()) {
            return false;
        }
        mNeedsShaderUpdate = false;
    }
    
    // 确保FBO已创建
    if (!mFrameBuffer) {
        // 从第一个输入获取尺寸
        if (auto tex = inputs[0]->getTexture()) {
            mOutputWidth = tex->GetWidth();
            mOutputHeight = tex->GetHeight();
        }
        if (!ensureFrameBuffer(mOutputWidth, mOutputHeight/*, mOutputFormat*/)) {
            return false;
        }
    }
    
    // TODO: 实际的GPU合成渲染
    // 1. 绑定FBO
    // mFrameBuffer->bind();
    
    // 2. 设置视口
    // glViewport(0, 0, mOutputWidth, mOutputHeight);
    // glClear(GL_COLOR_BUFFER_BIT);
    
    // 3. 使用着色器
    // mShaderProgram->use();
    
    // 4. 设置Uniforms
    // setUniforms(inputs[0].get());
    // mShaderProgram->setUniform("uBlendMode", static_cast<int>(mBlendMode));
    // mShaderProgram->setUniform("uLayout", static_cast<int>(mLayout));
    // mShaderProgram->setUniform("uPipRect", 
    //     mPipConfig.x, mPipConfig.y, mPipConfig.width, mPipConfig.height);
    
    // 5. 绑定输入纹理
    // for (size_t i = 0; i < inputs.size() && i < mInputConfigs.size(); ++i) {
    //     if (inputs[i] && inputs[i]->getTexture() && mInputConfigs[i].visible) {
    //         glActiveTexture(GL_TEXTURE0 + i);
    //         inputs[i]->getTexture()->bind();
    //         mShaderProgram->setUniform("uTexture" + std::to_string(i), (int)i);
    //         mShaderProgram->setUniform("uAlpha" + std::to_string(i), 
    //             mInputConfigs[i].alpha);
    //         mShaderProgram->setUniform("uUVTransform" + std::to_string(i),
    //             mInputConfigs[i].uvTransform[0],
    //             mInputConfigs[i].uvTransform[1],
    //             mInputConfigs[i].uvTransform[2],
    //             mInputConfigs[i].uvTransform[3]);
    //     }
    // }
    
    // 6. 绘制全屏四边形
    // drawFullscreenQuad();
    
    // 7. 解绑
    // mFrameBuffer->unbind();
    
    // 8. 设置输出纹理
    // output->setTexture(mFrameBuffer->getColorAttachment());
    
    // 合并元数据
//    for (size_t i = 0; i < inputs.size(); ++i) {
//        if (inputs[i]) {
//            // 复制元数据，添加输入索引前缀
//            auto& metadata = inputs[i]->getMetadata();
//            for (const auto& kv : metadata) {
//                output->setMetadata("input" + std::to_string(i) + "_" + kv.first, kv.second);
//            }
//        }
//    }
    
    return true;
}

} // namespace pipeline
