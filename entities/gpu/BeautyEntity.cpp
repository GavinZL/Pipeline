/**
 * @file BeautyEntity.cpp
 * @brief 美颜Entity实现 - 磨皮、美白等美颜效果
 */

#include "BeautyEntity.h"
#include "pipeline/data/FramePacket.h"
#include "pipeline/core/PipelineConfig.h"

#include <cmath>
#include <algorithm>

namespace pipeline {

// =============================================================================
// 着色器源码
// =============================================================================

namespace {

const char* kBeautyVertexShader = R"(
attribute vec4 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;

void main() {
    gl_Position = aPosition;
    vTexCoord = aTexCoord;
}
)";

// 双边滤波着色器 - 磨皮
const char* kBilateralFilterFragmentShader = R"(
precision highp float;
varying vec2 vTexCoord;

uniform sampler2D uInputTexture;
uniform vec2 uTexelSize;
uniform float uSmoothLevel;
uniform float uSmoothRadius;

// 双边滤波参数
const int KERNEL_SIZE = 9;
const float SIGMA_SPACE = 3.0;
const float SIGMA_COLOR = 0.1;

float gaussian(float x, float sigma) {
    return exp(-(x * x) / (2.0 * sigma * sigma));
}

void main() {
    vec4 centerColor = texture2D(uInputTexture, vTexCoord);
    
    if (uSmoothLevel <= 0.0) {
        gl_FragColor = centerColor;
        return;
    }
    
    vec3 result = vec3(0.0);
    float weightSum = 0.0;
    
    float radius = uSmoothRadius * uSmoothLevel;
    int kernelRadius = int(radius);
    
    for (int i = -KERNEL_SIZE / 2; i <= KERNEL_SIZE / 2; i++) {
        for (int j = -KERNEL_SIZE / 2; j <= KERNEL_SIZE / 2; j++) {
            vec2 offset = vec2(float(i), float(j)) * uTexelSize * radius / float(KERNEL_SIZE / 2);
            vec4 sampleColor = texture2D(uInputTexture, vTexCoord + offset);
            
            // 空间权重
            float spatialDist = length(vec2(float(i), float(j)));
            float spatialWeight = gaussian(spatialDist, SIGMA_SPACE);
            
            // 颜色权重
            float colorDist = length(centerColor.rgb - sampleColor.rgb);
            float colorWeight = gaussian(colorDist, SIGMA_COLOR);
            
            float weight = spatialWeight * colorWeight;
            result += sampleColor.rgb * weight;
            weightSum += weight;
        }
    }
    
    result /= weightSum;
    
    // 混合原图和磨皮效果
    vec3 finalColor = mix(centerColor.rgb, result, uSmoothLevel);
    gl_FragColor = vec4(finalColor, centerColor.a);
}
)";

// 高斯模糊着色器（用于多Pass模糊）
const char* kGaussianBlurHorizontalFragmentShader = R"(
precision highp float;
varying vec2 vTexCoord;

uniform sampler2D uInputTexture;
uniform vec2 uTexelSize;
uniform float uRadius;

void main() {
    vec4 result = vec4(0.0);
    float weightSum = 0.0;
    
    // 1D高斯权重
    float weights[9];
    weights[0] = 0.0162162162;
    weights[1] = 0.0540540541;
    weights[2] = 0.1216216216;
    weights[3] = 0.1945945946;
    weights[4] = 0.2270270270;
    weights[5] = 0.1945945946;
    weights[6] = 0.1216216216;
    weights[7] = 0.0540540541;
    weights[8] = 0.0162162162;
    
    for (int i = -4; i <= 4; i++) {
        vec2 offset = vec2(float(i) * uTexelSize.x * uRadius, 0.0);
        result += texture2D(uInputTexture, vTexCoord + offset) * weights[i + 4];
    }
    
    gl_FragColor = result;
}
)";

const char* kGaussianBlurVerticalFragmentShader = R"(
precision highp float;
varying vec2 vTexCoord;

uniform sampler2D uInputTexture;
uniform vec2 uTexelSize;
uniform float uRadius;

void main() {
    vec4 result = vec4(0.0);
    
    float weights[9];
    weights[0] = 0.0162162162;
    weights[1] = 0.0540540541;
    weights[2] = 0.1216216216;
    weights[3] = 0.1945945946;
    weights[4] = 0.2270270270;
    weights[5] = 0.1945945946;
    weights[6] = 0.1216216216;
    weights[7] = 0.0540540541;
    weights[8] = 0.0162162162;
    
    for (int i = -4; i <= 4; i++) {
        vec2 offset = vec2(0.0, float(i) * uTexelSize.y * uRadius);
        result += texture2D(uInputTexture, vTexCoord + offset) * weights[i + 4];
    }
    
    gl_FragColor = result;
}
)";

// 锐化着色器
const char* kSharpenFragmentShader = R"(
precision highp float;
varying vec2 vTexCoord;

uniform sampler2D uInputTexture;
uniform vec2 uTexelSize;
uniform float uSharpenLevel;

void main() {
    vec4 center = texture2D(uInputTexture, vTexCoord);
    
    if (uSharpenLevel <= 0.0) {
        gl_FragColor = center;
        return;
    }
    
    // USM锐化
    vec4 top = texture2D(uInputTexture, vTexCoord + vec2(0.0, -uTexelSize.y));
    vec4 bottom = texture2D(uInputTexture, vTexCoord + vec2(0.0, uTexelSize.y));
    vec4 left = texture2D(uInputTexture, vTexCoord + vec2(-uTexelSize.x, 0.0));
    vec4 right = texture2D(uInputTexture, vTexCoord + vec2(uTexelSize.x, 0.0));
    
    vec4 laplacian = 4.0 * center - top - bottom - left - right;
    vec4 sharpened = center + laplacian * uSharpenLevel;
    
    gl_FragColor = vec4(clamp(sharpened.rgb, 0.0, 1.0), center.a);
}
)";

// 美颜混合着色器 - 美白、红润等
const char* kBeautyBlendFragmentShader = R"(
precision highp float;
varying vec2 vTexCoord;

uniform sampler2D uInputTexture;
uniform sampler2D uBlurTexture;
uniform float uSmoothLevel;
uniform float uWhitenLevel;
uniform float uRuddyLevel;
uniform vec4 uFaceBounds;
uniform bool uUseFaceDetection;

// 皮肤检测
bool isSkin(vec3 color) {
    // 简单的肤色检测（基于RGB比值）
    float r = color.r;
    float g = color.g;
    float b = color.b;
    
    // 规则1：R > G > B
    if (r <= g || g <= b) return false;
    
    // 规则2：亮度范围
    float brightness = (r + g + b) / 3.0;
    if (brightness < 0.2 || brightness > 0.9) return false;
    
    // 规则3：肤色范围
    float rg = r - g;
    if (rg < 0.05 || rg > 0.4) return false;
    
    return true;
}

// 美白函数
vec3 whiten(vec3 color, float level) {
    // 使用曲线提亮
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float brightFactor = 1.0 + level * 0.3;
    
    // 非线性提亮，保护高光和暗部
    vec3 whitened = color * brightFactor;
    whitened = 1.0 - pow(1.0 - whitened, vec3(1.0 + level * 0.2));
    
    return clamp(whitened, 0.0, 1.0);
}

// 红润函数
vec3 ruddy(vec3 color, float level) {
    // 增加红色和黄色调
    color.r = color.r + level * 0.1;
    color.g = color.g + level * 0.03;
    return clamp(color, 0.0, 1.0);
}

void main() {
    vec4 originalColor = texture2D(uInputTexture, vTexCoord);
    vec4 blurColor = texture2D(uBlurTexture, vTexCoord);
    
    vec3 color = originalColor.rgb;
    
    // 检测是否在人脸区域
    bool inFaceRegion = true;
    if (uUseFaceDetection) {
        inFaceRegion = vTexCoord.x >= uFaceBounds.x && 
                       vTexCoord.x <= uFaceBounds.x + uFaceBounds.z &&
                       vTexCoord.y >= uFaceBounds.y && 
                       vTexCoord.y <= uFaceBounds.y + uFaceBounds.w;
    }
    
    // 检测是否为皮肤区域
    bool skinRegion = isSkin(color);
    
    // 只对皮肤区域应用美颜
    if (inFaceRegion && skinRegion) {
        // 磨皮：混合原图和模糊图
        if (uSmoothLevel > 0.0) {
            color = mix(color, blurColor.rgb, uSmoothLevel * 0.7);
        }
        
        // 美白
        if (uWhitenLevel > 0.0) {
            color = whiten(color, uWhitenLevel);
        }
        
        // 红润
        if (uRuddyLevel > 0.0) {
            color = ruddy(color, uRuddyLevel);
        }
    }
    
    gl_FragColor = vec4(color, originalColor.a);
}
)";

} // anonymous namespace

// =============================================================================
// BeautyEntity 实现
// =============================================================================

BeautyEntity::BeautyEntity(const std::string& name)
    : GPUEntity(name) {
    // 创建端口
    addInputPort("input");
    addOutputPort("output");
}

BeautyEntity::~BeautyEntity() = default;

// =============================================================================
// 磨皮参数
// =============================================================================

void BeautyEntity::setSmoothLevel(float level) {
    mSmoothLevel = std::clamp(level, 0.0f, 1.0f);
}

void BeautyEntity::setSmoothAlgorithm(BeautyAlgorithm algorithm) {
    if (mSmoothAlgorithm != algorithm) {
        mSmoothAlgorithm = algorithm;
//        mNeedsShaderUpdate = true;
    }
}

void BeautyEntity::setSmoothRadius(float radius) {
    mSmoothRadius = std::clamp(radius, 1.0f, 20.0f);
}

// =============================================================================
// 美白参数
// =============================================================================

void BeautyEntity::setWhitenLevel(float level) {
    mWhitenLevel = std::clamp(level, 0.0f, 1.0f);
}

// =============================================================================
// 红润参数
// =============================================================================

void BeautyEntity::setRuddyLevel(float level) {
    mRuddyLevel = std::clamp(level, 0.0f, 1.0f);
}

// =============================================================================
// 锐化参数
// =============================================================================

void BeautyEntity::setSharpenLevel(float level) {
    mSharpenLevel = std::clamp(level, 0.0f, 1.0f);
}

// =============================================================================
// 大眼瘦脸参数
// =============================================================================

void BeautyEntity::setEyeEnlargeLevel(float level) {
    mEyeEnlargeLevel = std::clamp(level, 0.0f, 1.0f);
}

void BeautyEntity::setFaceSlimLevel(float level) {
    mFaceSlimLevel = std::clamp(level, 0.0f, 1.0f);
}

// =============================================================================
// 预设
// =============================================================================

void BeautyEntity::setPreset(const std::string& presetName) {
    if (presetName == "natural") {
        mSmoothLevel = 0.3f;
        mWhitenLevel = 0.2f;
        mRuddyLevel = 0.1f;
        mSharpenLevel = 0.0f;
    } else if (presetName == "clear") {
        mSmoothLevel = 0.5f;
        mWhitenLevel = 0.4f;
        mRuddyLevel = 0.2f;
        mSharpenLevel = 0.1f;
    } else if (presetName == "goddess") {
        mSmoothLevel = 0.7f;
        mWhitenLevel = 0.5f;
        mRuddyLevel = 0.3f;
        mSharpenLevel = 0.15f;
    } else if (presetName == "none") {
        reset();
    }
}

void BeautyEntity::reset() {
    mSmoothLevel = 0.0f;
    mWhitenLevel = 0.0f;
    mRuddyLevel = 0.0f;
    mSharpenLevel = 0.0f;
    mEyeEnlargeLevel = 0.0f;
    mFaceSlimLevel = 0.0f;
}

void BeautyEntity::onParameterChanged(const std::string& key) {
    // 参数变化时的回调
}

// =============================================================================
// Shader设置
// =============================================================================

bool BeautyEntity::setupShader() {
    mVertexShaderSource = kBeautyVertexShader;
    mFragmentShaderSource = kBilateralFilterFragmentShader;
    
    // TODO: 创建着色器程序
    // mShaderProgram = mRenderContext->createShaderProgram(
    //     mVertexShaderSource, mFragmentShaderSource);
    
    // 创建其他着色器
    // mBilateralShader = mRenderContext->createShaderProgram(
    //     kBeautyVertexShader, kBilateralFilterFragmentShader);
    // mSharpenShader = mRenderContext->createShaderProgram(
    //     kBeautyVertexShader, kSharpenFragmentShader);
    // mBeautyBlendShader = mRenderContext->createShaderProgram(
    //     kBeautyVertexShader, kBeautyBlendFragmentShader);
    
    // 缓存Uniform位置
    // mSmoothLevelLocation = mBeautyBlendShader->getUniformLocation("uSmoothLevel");
    // mWhitenLevelLocation = mBeautyBlendShader->getUniformLocation("uWhitenLevel");
    // mRuddyLevelLocation = mBeautyBlendShader->getUniformLocation("uRuddyLevel");
    // mSharpenLevelLocation = mSharpenShader->getUniformLocation("uSharpenLevel");
    // mTexelSizeLocation = mBilateralShader->getUniformLocation("uTexelSize");
    // mFaceBoundsLocation = mBeautyBlendShader->getUniformLocation("uFaceBounds");
    
    return true;
}

void BeautyEntity::setUniforms(FramePacket* input) {
    if (!mShaderProgram) return;
    
    // 设置纹素大小
    if (input && input->getTexture()) {
//        float texelX = 1.0f / input->getTexture()->getWidth();
//        float texelY = 1.0f / input->getTexture()->getHeight();
//         mShaderProgram->setUniform(mTexelSizeLocation, texelX, texelY);
    }
}

bool BeautyEntity::createBlurTextures() {
    if (!mRenderContext) return false;
    
    // TODO: 创建模糊中间纹理
    // mBlurTexture1 = mRenderContext->createTexture(mOutputWidth, mOutputHeight, PixelFormat::RGBA8);
    // mBlurTexture2 = mRenderContext->createTexture(mOutputWidth, mOutputHeight, PixelFormat::RGBA8);
    // mBlurFBO1 = mRenderContext->createFrameBuffer();
    // mBlurFBO1->attachColorTexture(mBlurTexture1);
    // mBlurFBO2 = mRenderContext->createFrameBuffer();
    // mBlurFBO2->attachColorTexture(mBlurTexture2);
    
    return true;
}

void BeautyEntity::performBilateralFilter(
    std::shared_ptr<lrengine::render::LRTexture> input,
    std::shared_ptr<lrengine::render::LRTexture> output) {
    
    // TODO: 执行双边滤波
    // 这里可以分两Pass实现，先水平后垂直，以提高性能
    
    // Pass 1: 水平
    // mBlurFBO1->bind();
    // mBilateralShader->use();
    // mBilateralShader->setUniform("uInputTexture", input, 0);
    // mBilateralShader->setUniform("uSmoothLevel", mSmoothLevel);
    // mBilateralShader->setUniform("uSmoothRadius", mSmoothRadius);
    // drawFullscreenQuad();
    
    // Pass 2: 垂直
    // mBlurFBO2->bind();
    // ... (使用mBlurTexture1作为输入)
}

void BeautyEntity::generateGaussianWeights(float sigma, std::vector<float>& weights) {
    int radius = static_cast<int>(std::ceil(sigma * 3.0f));
    weights.resize(radius * 2 + 1);
    
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        float w = std::exp(-(i * i) / (2.0f * sigma * sigma));
        weights[i + radius] = w;
        sum += w;
    }
    
    // 归一化
    for (auto& w : weights) {
        w /= sum;
    }
}

bool BeautyEntity::readFaceInfo(FramePacket* packet) {
    if (!packet || !mUseFaceDetection) {
        mCurrentFace.valid = false;
        return false;
    }
    
    // 从metadata读取人脸信息
    auto faces = packet->getMetadata<std::string>(mFaceMetadataKey);
    if (!faces) {
        mCurrentFace.valid = false;
        return false;
    }
    
    // TODO: 解析人脸信息JSON或结构体
    // 假设格式为 "x,y,w,h"
    // sscanf(faces->c_str(), "%f,%f,%f,%f", 
    //     &mCurrentFace.boundingBox[0],
    //     &mCurrentFace.boundingBox[1],
    //     &mCurrentFace.boundingBox[2],
    //     &mCurrentFace.boundingBox[3]);
    
    mCurrentFace.valid = true;
    return true;
}

// =============================================================================
// GPU处理
// =============================================================================

bool BeautyEntity::processGPU(const std::vector<FramePacketPtr>& inputs,
                              FramePacketPtr output) {
    if (inputs.empty() || !inputs[0] || !output) {
        return false;
    }
//
//    auto input = inputs[0];
//    auto inputTexture = input->getTexture();
//    if (!inputTexture) {
//        return false;
//    }
//
//    // 如果所有美颜参数都为0，直接复制输入
//    if (mSmoothLevel <= 0.0f && mWhitenLevel <= 0.0f &&
//        mRuddyLevel <= 0.0f && mSharpenLevel <= 0.0f) {
//        output->setTexture(inputTexture);
//        return true;
//    }
//
//    // 确保着色器已创建
//    if (!mShaderProgram || mNeedsShaderUpdate) {
//        if (!setupShader()) {
//            return false;
//        }
//        mNeedsShaderUpdate = false;
//    }
//
//    // 读取人脸信息
//    readFaceInfo(input.get());
//
//    uint32_t width = inputTexture->getWidth();
//    uint32_t height = inputTexture->getHeight();
//
//    // 确保中间纹理已创建
//    if (!mBlurTexture1 || mBlurTexture1->getWidth() != width) {
//        if (!createBlurTextures()) {
//            return false;
//        }
//    }
//
//    // 确保FBO已创建
//    if (!ensureFrameBuffer(width, height, mOutputFormat)) {
//        return false;
//    }
//
//    // TODO: 多Pass渲染
//
//    // Pass 1: 磨皮（双边滤波）
//    if (mSmoothLevel > 0.0f) {
//        // mBlurFBO1->bind();
//        // glViewport(0, 0, width, height);
//        // mBilateralShader->use();
//        // inputTexture->bind(0);
//        // mBilateralShader->setUniform("uInputTexture", 0);
//        // mBilateralShader->setUniform("uTexelSize", 1.0f/width, 1.0f/height);
//        // mBilateralShader->setUniform("uSmoothLevel", mSmoothLevel);
//        // mBilateralShader->setUniform("uSmoothRadius", mSmoothRadius);
//        // drawFullscreenQuad();
//    }
//
//    // Pass 2: 美颜混合（美白、红润）
//    // mFrameBuffer->bind();
//    // glViewport(0, 0, width, height);
//    // mBeautyBlendShader->use();
//    // inputTexture->bind(0);
//    // mBeautyBlendShader->setUniform("uInputTexture", 0);
//    // if (mSmoothLevel > 0.0f) {
//    //     mBlurTexture1->bind(1);
//    //     mBeautyBlendShader->setUniform("uBlurTexture", 1);
//    // }
//    // mBeautyBlendShader->setUniform("uSmoothLevel", mSmoothLevel);
//    // mBeautyBlendShader->setUniform("uWhitenLevel", mWhitenLevel);
//    // mBeautyBlendShader->setUniform("uRuddyLevel", mRuddyLevel);
//    // mBeautyBlendShader->setUniform("uUseFaceDetection", mUseFaceDetection);
//    // if (mCurrentFace.valid) {
//    //     mBeautyBlendShader->setUniform("uFaceBounds",
//    //         mCurrentFace.boundingBox[0], mCurrentFace.boundingBox[1],
//    //         mCurrentFace.boundingBox[2], mCurrentFace.boundingBox[3]);
//    // }
//    // drawFullscreenQuad();
//
//    // Pass 3: 锐化（可选）
//    if (mSharpenLevel > 0.0f) {
//        // 需要再一个Pass，或者合并到上一个Pass
//        // mBlurFBO2->bind();
//        // mSharpenShader->use();
//        // ...
//    }
//
//    // 设置输出纹理
//    // output->setTexture(mFrameBuffer->getColorAttachment());
//
//    // 添加美颜参数到元数据
//    output->setMetadata("beauty_smooth", mSmoothLevel);
//    output->setMetadata("beauty_whiten", mWhitenLevel);
//    output->setMetadata("beauty_ruddy", mRuddyLevel);
//    output->setMetadata("beauty_sharpen", mSharpenLevel);
//
    return true;
}

} // namespace pipeline
