/**
 * @file FilterEntity.cpp
 * @brief LUT滤镜Entity实现 - 使用查找表进行色彩校正
 */

#include "FilterEntity.h"
#include "pipeline/data/FramePacket.h"
#include "pipeline/core/PipelineConfig.h"
#include "lrengine/core/LRTexture.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace pipeline {

// =============================================================================
// 着色器源码
// =============================================================================

namespace {

const char* kFilterVertexShader = R"(
attribute vec4 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;

void main() {
    gl_Position = aPosition;
    vTexCoord = aTexCoord;
}
)";

// 3D LUT着色器
const char* kLUT3DFragmentShader = R"(
precision highp float;
varying vec2 vTexCoord;

uniform sampler2D uInputTexture;
uniform sampler2D uLUTTexture;
uniform float uIntensity;
uniform float uLUTSize;
uniform float uBrightness;
uniform float uContrast;
uniform float uSaturation;

// 色彩校正
vec3 adjustBrightness(vec3 color, float brightness) {
    return color + brightness;
}

vec3 adjustContrast(vec3 color, float contrast) {
    return (color - 0.5) * contrast + 0.5;
}

vec3 adjustSaturation(vec3 color, float saturation) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(luminance), color, saturation);
}

// 3D LUT采样
vec3 sampleLUT(vec3 color, float size) {
    // LUT作为2D纹理存储，需要计算正确的UV
    float sliceSize = 1.0 / size;
    float slicePixelSize = sliceSize / size;
    float sliceInnerSize = slicePixelSize * (size - 1.0);
    
    float blueSlice0 = floor(color.b * (size - 1.0));
    float blueSlice1 = min(blueSlice0 + 1.0, size - 1.0);
    
    // 计算两个切片的UV坐标
    vec2 uv0, uv1;
    uv0.x = blueSlice0 * sliceSize + slicePixelSize * 0.5 + color.r * sliceInnerSize;
    uv0.y = slicePixelSize * 0.5 + color.g * sliceInnerSize;
    
    uv1.x = blueSlice1 * sliceSize + slicePixelSize * 0.5 + color.r * sliceInnerSize;
    uv1.y = slicePixelSize * 0.5 + color.g * sliceInnerSize;
    
    // 采样两个切片并插值
    vec3 lutColor0 = texture2D(uLUTTexture, uv0).rgb;
    vec3 lutColor1 = texture2D(uLUTTexture, uv1).rgb;
    
    float blueFrac = fract(color.b * (size - 1.0));
    return mix(lutColor0, lutColor1, blueFrac);
}

void main() {
    vec4 originalColor = texture2D(uInputTexture, vTexCoord);
    vec3 color = originalColor.rgb;
    
    // 应用基础色彩调整
    color = adjustBrightness(color, uBrightness);
    color = adjustContrast(color, uContrast);
    color = adjustSaturation(color, uSaturation);
    color = clamp(color, 0.0, 1.0);
    
    // 应用LUT
    if (uLUTSize > 0.0) {
        vec3 lutColor = sampleLUT(color, uLUTSize);
        color = mix(color, lutColor, uIntensity);
    }
    
    gl_FragColor = vec4(color, originalColor.a);
}
)";

// 颜色矩阵着色器
const char* kColorMatrixFragmentShader = R"(
precision highp float;
varying vec2 vTexCoord;

uniform sampler2D uInputTexture;
uniform mat4 uColorMatrix;
uniform float uIntensity;
uniform float uBrightness;
uniform float uContrast;
uniform float uSaturation;

vec3 adjustBrightness(vec3 color, float brightness) {
    return color + brightness;
}

vec3 adjustContrast(vec3 color, float contrast) {
    return (color - 0.5) * contrast + 0.5;
}

vec3 adjustSaturation(vec3 color, float saturation) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(luminance), color, saturation);
}

void main() {
    vec4 originalColor = texture2D(uInputTexture, vTexCoord);
    vec3 color = originalColor.rgb;
    
    // 应用基础色彩调整
    color = adjustBrightness(color, uBrightness);
    color = adjustContrast(color, uContrast);
    color = adjustSaturation(color, uSaturation);
    
    // 应用颜色矩阵
    vec4 matrixColor = uColorMatrix * vec4(color, 1.0);
    color = mix(color, matrixColor.rgb, uIntensity);
    
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), originalColor.a);
}
)";

} // anonymous namespace

// =============================================================================
// FilterEntity 实现
// =============================================================================

FilterEntity::FilterEntity(const std::string& name)
    : GPUEntity(name) {
    // 创建端口
    addInputPort("input");
    addOutputPort("output");
}

FilterEntity::~FilterEntity() = default;

// =============================================================================
// LUT加载
// =============================================================================

bool FilterEntity::loadLUTFromFile(const std::string& path) {
    // 检查文件扩展名
    std::string ext;
    size_t dotPos = path.rfind('.');
    if (dotPos != std::string::npos) {
        ext = path.substr(dotPos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    
    if (ext == ".cube") {
        return parseCubeFile(path);
    } else if (ext == ".3dl") {
        // TODO: 实现.3dl格式解析
        return false;
    }
    
    return false;
}

bool FilterEntity::parseCubeFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    uint32_t lutSize = 0;
    std::vector<float> lutData;
    
    while (std::getline(file, line)) {
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // 解析LUT大小
        if (line.find("LUT_3D_SIZE") != std::string::npos) {
            std::istringstream iss(line);
            std::string token;
            iss >> token >> lutSize;
            lutData.reserve(lutSize * lutSize * lutSize * 3);
            continue;
        }
        
        // 跳过其他头部信息
        if (line.find("TITLE") != std::string::npos ||
            line.find("DOMAIN_MIN") != std::string::npos ||
            line.find("DOMAIN_MAX") != std::string::npos) {
            continue;
        }
        
        // 解析RGB值
        std::istringstream iss(line);
        float r, g, b;
        if (iss >> r >> g >> b) {
            lutData.push_back(r);
            lutData.push_back(g);
            lutData.push_back(b);
        }
    }
    
    if (lutSize == 0 || lutData.empty()) {
        return false;
    }
    
    mLUTType = LUTType::LUT3D;
    mLUTSize = lutSize;
    mLUTData = std::move(lutData);
    mLUTNeedsUpdate = true;
    mNeedsShaderUpdate = true;
    
    return true;
}

bool FilterEntity::loadLUT3D(const uint8_t* data, uint32_t size) {
    if (!data || size == 0) {
        return false;
    }
    
    // 转换uint8到float
    size_t totalSize = size * size * size * 3;
    mLUTData.resize(totalSize);
    
    for (size_t i = 0; i < totalSize; ++i) {
        mLUTData[i] = data[i] / 255.0f;
    }
    
    mLUTType = LUTType::LUT3D;
    mLUTSize = size;
    mLUTNeedsUpdate = true;
    mNeedsShaderUpdate = true;
    
    return true;
}

bool FilterEntity::loadLUT3DFloat(const float* data, uint32_t size) {
    if (!data || size == 0) {
        return false;
    }
    
    size_t totalSize = size * size * size * 3;
    mLUTData.assign(data, data + totalSize);
    
    mLUTType = LUTType::LUT3D;
    mLUTSize = size;
    mLUTNeedsUpdate = true;
    mNeedsShaderUpdate = true;
    
    return true;
}

void FilterEntity::setColorMatrix(const float matrix[16]) {
    std::memcpy(mColorMatrix, matrix, 16 * sizeof(float));
    mLUTType = LUTType::ColorMatrix;
    mNeedsShaderUpdate = true;
}

bool FilterEntity::setPreset(const std::string& presetName) {
    // 内置预设滤镜
    if (presetName == "normal" || presetName == "none") {
        // 重置为原始
        mIntensity = 0.0f;
        return true;
    }
    
    if (presetName == "warm") {
        // 暖色调
        mTemperature = 0.3f;
        mTint = 0.1f;
        updateColorCorrection();
        return true;
    }
    
    if (presetName == "cool") {
        // 冷色调
        mTemperature = -0.3f;
        mTint = -0.1f;
        updateColorCorrection();
        return true;
    }
    
    if (presetName == "vivid") {
        // 鲜艳
        mSaturation = 1.3f;
        mContrast = 1.1f;
        return true;
    }
    
    if (presetName == "vintage") {
        // 复古
        mSaturation = 0.8f;
        mContrast = 0.9f;
        // 设置偏黄色调矩阵
        float vintageMatrix[16] = {
            1.2f, 0.1f, 0.0f, 0.0f,
            0.0f, 1.1f, 0.1f, 0.0f,
            0.0f, 0.0f, 0.8f, 0.0f,
            0.05f, 0.05f, -0.05f, 1.0f
        };
        setColorMatrix(vintageMatrix);
        return true;
    }
    
    if (presetName == "bw" || presetName == "blackwhite") {
        // 黑白
        mSaturation = 0.0f;
        return true;
    }
    
    return false;
}

// =============================================================================
// 参数配置
// =============================================================================

void FilterEntity::setIntensity(float intensity) {
    mIntensity = std::clamp(intensity, 0.0f, 1.0f);
}

void FilterEntity::setBrightness(float brightness) {
    mBrightness = std::clamp(brightness, -1.0f, 1.0f);
}

void FilterEntity::setContrast(float contrast) {
    mContrast = std::clamp(contrast, 0.0f, 2.0f);
}

void FilterEntity::setSaturation(float saturation) {
    mSaturation = std::clamp(saturation, 0.0f, 2.0f);
}

void FilterEntity::setTemperature(float temperature) {
    mTemperature = std::clamp(temperature, -1.0f, 1.0f);
    updateColorCorrection();
}

void FilterEntity::setTint(float tint) {
    mTint = std::clamp(tint, -1.0f, 1.0f);
    updateColorCorrection();
}

void FilterEntity::updateColorCorrection() {
    // 根据色温和色调更新颜色矩阵
    // 色温影响红/蓝通道
    float tempScale = 0.2f * mTemperature;
    // 色调影响绿通道
    float tintScale = 0.1f * mTint;
    
    mColorMatrix[0] = 1.0f + tempScale;  // R
    mColorMatrix[5] = 1.0f + tintScale;  // G
    mColorMatrix[10] = 1.0f - tempScale; // B
}

void FilterEntity::onParameterChanged(const std::string& key) {
    // 参数变化时的回调
}

// =============================================================================
// Shader设置
// =============================================================================

bool FilterEntity::setupShader() {
    mVertexShaderSource = kFilterVertexShader;
    
    // 根据LUT类型选择Fragment Shader
    if (mLUTType == LUTType::LUT3D) {
        mFragmentShaderSource = kLUT3DFragmentShader;
    } else {
        mFragmentShaderSource = kColorMatrixFragmentShader;
    }
    
    // TODO: 使用LREngine创建着色器
    // mShaderProgram = mRenderContext->createShaderProgram(
    //     mVertexShaderSource, mFragmentShaderSource);
    
    // 缓存Uniform位置
    // mIntensityLocation = mShaderProgram->getUniformLocation("uIntensity");
    // mBrightnessLocation = mShaderProgram->getUniformLocation("uBrightness");
    // mContrastLocation = mShaderProgram->getUniformLocation("uContrast");
    // mSaturationLocation = mShaderProgram->getUniformLocation("uSaturation");
    // mColorMatrixLocation = mShaderProgram->getUniformLocation("uColorMatrix");
    // mLUTSizeLocation = mShaderProgram->getUniformLocation("uLUTSize");
    
    return true;
}

void FilterEntity::setUniforms(FramePacket* input) {
    if (!mShaderProgram) return;
    
    // TODO: 设置Uniforms
    // mShaderProgram->setUniform(mIntensityLocation, mIntensity);
    // mShaderProgram->setUniform(mBrightnessLocation, mBrightness);
    // mShaderProgram->setUniform(mContrastLocation, mContrast);
    // mShaderProgram->setUniform(mSaturationLocation, mSaturation);
    
    // if (mLUTType == LUTType::LUT3D) {
    //     mShaderProgram->setUniform(mLUTSizeLocation, (float)mLUTSize);
    // } else {
    //     mShaderProgram->setUniformMatrix(mColorMatrixLocation, mColorMatrix);
    // }
}

bool FilterEntity::createLUTTexture() {
    if (mLUTData.empty() || mLUTSize == 0) {
        return false;
    }
    
    // TODO: 创建LUT纹理
    // 3D LUT存储为2D纹理，尺寸为 (size * size, size)
    // uint32_t texWidth = mLUTSize * mLUTSize;
    // uint32_t texHeight = mLUTSize;
    // 
    // mLUTTexture = mRenderContext->createTexture(texWidth, texHeight, PixelFormat::RGB8);
    // mLUTTexture->upload(mLUTData.data());
    // mLUTTexture->setFilter(FilterMode::Linear, FilterMode::Linear);
    
    mLUTNeedsUpdate = false;
    return true;
}

// =============================================================================
// GPU处理
// =============================================================================

bool FilterEntity::processGPU(const std::vector<FramePacketPtr>& inputs,
                              FramePacketPtr output) {
    if (inputs.empty() || !inputs[0] || !output) {
        return false;
    }
    
    auto input = inputs[0];
    auto inputTexture = input->getTexture();
    if (!inputTexture) {
        return false;
    }
    
    // 确保着色器已创建
    if (!mShaderProgram || mNeedsShaderUpdate) {
        if (!setupShader()) {
            return false;
        }
        mNeedsShaderUpdate = false;
    }
    
    // 确保LUT纹理已创建
    if (mLUTType == LUTType::LUT3D && mLUTNeedsUpdate) {
        if (!createLUTTexture()) {
            return false;
        }
    }
    
    // 确保FBO已创建
    uint32_t width = inputTexture->GetWidth();
    uint32_t height = inputTexture->GetHeight();
    if (!ensureFrameBuffer(width, height)) {
        return false;
    }
    
    // TODO: 实际GPU渲染
    // 1. 绑定FBO
    // mFrameBuffer->bind();
    // glViewport(0, 0, width, height);
    
    // 2. 使用着色器
    // mShaderProgram->use();
    
    // 3. 设置Uniforms
    // setUniforms(input.get());
    
    // 4. 绑定输入纹理
    // glActiveTexture(GL_TEXTURE0);
    // inputTexture->bind();
    // mShaderProgram->setUniform("uInputTexture", 0);
    
    // 5. 绑定LUT纹理
    // if (mLUTTexture) {
    //     glActiveTexture(GL_TEXTURE1);
    //     mLUTTexture->bind();
    //     mShaderProgram->setUniform("uLUTTexture", 1);
    // }
    
    // 6. 绘制
    // drawFullscreenQuad();
    
    // 7. 解绑
    // mFrameBuffer->unbind();
    
    // 8. 设置输出纹理
    // output->setTexture(mFrameBuffer->getColorAttachment());
    
    // 复制元数据
    output->setMetadata("filter_intensity", mIntensity);
    output->setMetadata("filter_brightness", mBrightness);
    output->setMetadata("filter_contrast", mContrast);
    output->setMetadata("filter_saturation", mSaturation);
    
    return true;
}

} // namespace pipeline
