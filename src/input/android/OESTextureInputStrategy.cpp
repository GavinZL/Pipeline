/**
 * @file OESTextureInputStrategy.cpp
 * @brief OESTextureInputStrategy 实现
 */

#ifdef __ANDROID__

#include "pipeline/input/android/OESTextureInputStrategy.h"
#include "pipeline/utils/PipelineLog.h"

namespace pipeline {
namespace input {
namespace android {

// OES 转换 Vertex Shader
static const char* OES_VERTEX_SHADER = R"(
#version 300 es
layout(location = 0) in vec4 aPosition;
layout(location = 1) in vec2 aTexCoord;

uniform mat4 uTransformMatrix;

out vec2 vTexCoord;

void main() {
    gl_Position = aPosition;
    vTexCoord = (uTransformMatrix * vec4(aTexCoord, 0.0, 1.0)).xy;
}
)";

// OES 转换 Fragment Shader
static const char* OES_FRAGMENT_SHADER = R"(
#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;

uniform samplerExternalOES uOESTexture;

in vec2 vTexCoord;
out vec4 fragColor;

void main() {
    fragColor = texture(uOESTexture, vTexCoord);
}
)";

// 全屏四边形顶点数据
static const float QUAD_VERTICES[] = {
    // Position    // TexCoord
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
    -1.0f,  1.0f,  0.0f, 1.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
};

// =============================================================================
// 构造与析构
// =============================================================================

OESTextureInputStrategy::OESTextureInputStrategy() = default;

OESTextureInputStrategy::~OESTextureInputStrategy() {
    release();
}

// =============================================================================
// InputStrategy 接口实现
// =============================================================================

bool OESTextureInputStrategy::initialize(lrengine::render::LRRenderContext* context) {
    if (mInitialized) {
        return true;
    }
    
    mRenderContext = context;
    
    // 初始化 OES shader
    if (!initializeOESShader()) {
        PIPELINE_LOGE("Failed to initialize OES shader");
        return false;
    }
    
    mInitialized = true;
    PIPELINE_LOGI("OESTextureInputStrategy initialized");
    return true;
}

bool OESTextureInputStrategy::processToGPU(const InputData& input,
                                            lrengine::LRTexturePtr& outputTexture) {
    if (!mInitialized) {
        return false;
    }
    
    const auto& gpu = input.gpu;
    
    // 确保 FBO 尺寸正确
    if (!initializeFBO(gpu.width, gpu.height)) {
        return false;
    }
    
    // 转换 OES 纹理
    if (!convertOESToTexture2D(gpu.textureId, gpu.width, gpu.height,
                               gpu.transformMatrix)) {
        return false;
    }
    
    // 返回输出纹理
    outputTexture = mOutputTextureWrapper;
    return true;
}

bool OESTextureInputStrategy::processToCPU(const InputData& input,
                                            uint8_t* outputBuffer,
                                            size_t& outputSize) {
    if (!mInitialized || !outputBuffer) {
        return false;
    }
    
    const auto& gpu = input.gpu;
    
    // 先渲染到 FBO
    if (!initializeFBO(gpu.width, gpu.height)) {
        return false;
    }
    
    if (!convertOESToTexture2D(gpu.textureId, gpu.width, gpu.height,
                               gpu.transformMatrix)) {
        return false;
    }
    
    // 回读像素
    size_t requiredSize = gpu.width * gpu.height * 4; // RGBA
    if (outputSize < requiredSize) {
        outputSize = requiredSize;
        return false;
    }
    
    if (!readbackPixels(outputBuffer, outputSize, gpu.width, gpu.height)) {
        return false;
    }
    
    outputSize = requiredSize;
    return true;
}

void OESTextureInputStrategy::release() {
    cleanupGPUResources();
    mInitialized = false;
    PIPELINE_LOGI("OESTextureInputStrategy released");
}

// =============================================================================
// Android 特定配置
// =============================================================================

void OESTextureInputStrategy::setEGLContextManager(AndroidEGLContextManager* manager) {
    mEGLManager = manager;
}

// =============================================================================
// 内部方法
// =============================================================================

bool OESTextureInputStrategy::initializeOESShader() {
    // 创建 Vertex Shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &OES_VERTEX_SHADER, nullptr);
    glCompileShader(vertexShader);
    
    GLint success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
        PIPELINE_LOGE("OES vertex shader compilation failed: %s", infoLog);
        glDeleteShader(vertexShader);
        return false;
    }
    
    // 创建 Fragment Shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &OES_FRAGMENT_SHADER, nullptr);
    glCompileShader(fragmentShader);
    
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
        PIPELINE_LOGE("OES fragment shader compilation failed: %s", infoLog);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }
    
    // 创建程序
    mOESShaderProgram = glCreateProgram();
    glAttachShader(mOESShaderProgram, vertexShader);
    glAttachShader(mOESShaderProgram, fragmentShader);
    glLinkProgram(mOESShaderProgram);
    
    glGetProgramiv(mOESShaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(mOESShaderProgram, 512, nullptr, infoLog);
        PIPELINE_LOGE("OES shader program linking failed: %s", infoLog);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteProgram(mOESShaderProgram);
        mOESShaderProgram = 0;
        return false;
    }
    
    // 清理 shader（已链接到程序）
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    // 获取 uniform locations
    mOESTextureLocation = glGetUniformLocation(mOESShaderProgram, "uOESTexture");
    mTransformMatrixLocation = glGetUniformLocation(mOESShaderProgram, "uTransformMatrix");
    mPositionLocation = 0;
    mTexCoordLocation = 1;
    
    // 创建 VAO/VBO
    glGenVertexArrays(1, &mVAO);
    glGenBuffers(1, &mVBO);
    
    glBindVertexArray(mVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTICES), QUAD_VERTICES, GL_STATIC_DRAW);
    
    // Position
    glVertexAttribPointer(mPositionLocation, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(mPositionLocation);
    
    // TexCoord
    glVertexAttribPointer(mTexCoordLocation, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 
                          (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(mTexCoordLocation);
    
    glBindVertexArray(0);
    
    return true;
}

bool OESTextureInputStrategy::initializeFBO(uint32_t width, uint32_t height) {
    if (mFBO != 0 && mFBOWidth == width && mFBOHeight == height) {
        return true; // 已存在且尺寸匹配
    }
    
    // 清理旧资源
    if (mFBO != 0) {
        glDeleteFramebuffers(1, &mFBO);
        mFBO = 0;
    }
    if (mOutputTexture != 0) {
        glDeleteTextures(1, &mOutputTexture);
        mOutputTexture = 0;
    }
    
    // 创建输出纹理
    glGenTextures(1, &mOutputTexture);
    glBindTexture(GL_TEXTURE_2D, mOutputTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // 创建 FBO
    glGenFramebuffers(1, &mFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mOutputTexture, 0);
    
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        PIPELINE_LOGE("FBO not complete: 0x%x", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    mFBOWidth = width;
    mFBOHeight = height;
    
    // TODO: 创建 LRTexture 包装
    // mOutputTextureWrapper = ...
    
    return true;
}

bool OESTextureInputStrategy::convertOESToTexture2D(uint32_t oesTextureId,
                                                     uint32_t width, uint32_t height,
                                                     const float* transformMatrix) {
    // 绑定 FBO
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glViewport(0, 0, width, height);
    
    // 使用 OES shader
    glUseProgram(mOESShaderProgram);
    
    // 绑定 OES 纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesTextureId);
    glUniform1i(mOESTextureLocation, 0);
    
    // 设置变换矩阵
    if (transformMatrix) {
        glUniformMatrix4fv(mTransformMatrixLocation, 1, GL_FALSE, transformMatrix);
    } else {
        // 单位矩阵
        static const float identity[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        glUniformMatrix4fv(mTransformMatrixLocation, 1, GL_FALSE, identity);
    }
    
    // 绘制
    glBindVertexArray(mVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    
    // 解绑
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    
    return true;
}

bool OESTextureInputStrategy::readbackPixels(uint8_t* buffer, size_t bufferSize,
                                              uint32_t width, uint32_t height) {
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    
    // 同步读取（简单实现）
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        PIPELINE_LOGE("glReadPixels error: 0x%x", error);
        return false;
    }
    
    return true;
}

void OESTextureInputStrategy::cleanupGPUResources() {
    if (mOESShaderProgram != 0) {
        glDeleteProgram(mOESShaderProgram);
        mOESShaderProgram = 0;
    }
    
    if (mVAO != 0) {
        glDeleteVertexArrays(1, &mVAO);
        mVAO = 0;
    }
    
    if (mVBO != 0) {
        glDeleteBuffers(1, &mVBO);
        mVBO = 0;
    }
    
    if (mFBO != 0) {
        glDeleteFramebuffers(1, &mFBO);
        mFBO = 0;
    }
    
    if (mOutputTexture != 0) {
        glDeleteTextures(1, &mOutputTexture);
        mOutputTexture = 0;
    }
    
    if (mPBO != 0) {
        glDeleteBuffers(1, &mPBO);
        mPBO = 0;
    }
    
    mOutputTextureWrapper.reset();
}

} // namespace android
} // namespace input
} // namespace pipeline

#endif // __ANDROID__
