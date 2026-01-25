/**
 * @file AndroidEGLSurface.cpp
 * @brief AndroidEGLSurface 实现
 */

#ifdef __ANDROID__

#include "pipeline/output/android/AndroidEGLSurface.h"
#include "pipeline/utils/PipelineLog.h"

namespace pipeline {
namespace output {
namespace android {

// 显示 Vertex Shader
static const char* DISPLAY_VERTEX_SHADER = R"(
#version 300 es
layout(location = 0) in vec4 aPosition;
layout(location = 1) in vec2 aTexCoord;

uniform mat4 uTransform;

out vec2 vTexCoord;

void main() {
    gl_Position = uTransform * aPosition;
    vTexCoord = aTexCoord;
}
)";

// 显示 Fragment Shader
static const char* DISPLAY_FRAGMENT_SHADER = R"(
#version 300 es
precision mediump float;

uniform sampler2D uTexture;

in vec2 vTexCoord;
out vec4 fragColor;

void main() {
    fragColor = texture(uTexture, vTexCoord);
}
)";

// 全屏四边形顶点
static const float SCREEN_QUAD[] = {
    // Position    // TexCoord
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
    -1.0f,  1.0f,  0.0f, 1.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
};

// =============================================================================
// 构造与析构
// =============================================================================

AndroidEGLSurface::AndroidEGLSurface() = default;

AndroidEGLSurface::~AndroidEGLSurface() {
    release();
}

// =============================================================================
// DisplaySurface 接口实现
// =============================================================================

bool AndroidEGLSurface::initialize(lrengine::render::LRRenderContext* context) {
    mRenderContext = context;
    
    if (!mEGLManager) {
        PIPELINE_LOGE("EGLContextManager not set");
        return false;
    }
    
    // 获取 EGL display
    mEGLDisplay = mEGLManager->getDisplay();
    if (mEGLDisplay == EGL_NO_DISPLAY) {
        PIPELINE_LOGE("Invalid EGL display");
        return false;
    }
    
    // 如果已经绑定了窗口，创建 surface
    if (mNativeWindow) {
        if (!createEGLWindowSurface()) {
            return false;
        }
    }
    
    mState = SurfaceState::Ready;
    PIPELINE_LOGI("AndroidEGLSurface initialized");
    return true;
}

void AndroidEGLSurface::release() {
    cleanupRenderResources();
    destroyEGLSurface();
    
    if (mNativeWindow) {
        ANativeWindow_release(mNativeWindow);
        mNativeWindow = nullptr;
    }
    
    mState = SurfaceState::Uninitialized;
    PIPELINE_LOGI("AndroidEGLSurface released");
}

bool AndroidEGLSurface::attachToWindow(void* window) {
    if (!window) {
        return false;
    }
    
    // 释放旧窗口
    if (mNativeWindow) {
        destroyEGLSurface();
        ANativeWindow_release(mNativeWindow);
    }
    
    mNativeWindow = static_cast<ANativeWindow*>(window);
    ANativeWindow_acquire(mNativeWindow);
    
    // 获取窗口尺寸
    mWidth = ANativeWindow_getWidth(mNativeWindow);
    mHeight = ANativeWindow_getHeight(mNativeWindow);
    
    // 如果已初始化，创建 surface
    if (mEGLDisplay != EGL_NO_DISPLAY) {
        if (!createEGLWindowSurface()) {
            return false;
        }
    }
    
    PIPELINE_LOGI("Attached to window: %dx%d", mWidth, mHeight);
    return true;
}

void AndroidEGLSurface::detach() {
    destroyEGLSurface();
    
    if (mNativeWindow) {
        ANativeWindow_release(mNativeWindow);
        mNativeWindow = nullptr;
    }
    
    mWidth = 0;
    mHeight = 0;
}

SurfaceSize AndroidEGLSurface::getSize() const {
    return SurfaceSize{mWidth, mHeight, 1.0f};
}

void AndroidEGLSurface::setSize(uint32_t width, uint32_t height) {
    if (mWidth != width || mHeight != height) {
        onSizeChanged(width, height);
    }
}

void AndroidEGLSurface::onSizeChanged(uint32_t width, uint32_t height) {
    mWidth = width;
    mHeight = height;
    
    // 可能需要重建 surface
    if (mEGLSurface != EGL_NO_SURFACE) {
        destroyEGLSurface();
        createEGLWindowSurface();
    }
    
    PIPELINE_LOGI("Surface size changed: %dx%d", width, height);
}

bool AndroidEGLSurface::beginFrame() {
    if (mState != SurfaceState::Ready) {
        return false;
    }
    
    if (mEGLSurface == EGL_NO_SURFACE) {
        return false;
    }
    
    // 激活上下文
    if (!eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface, 
                        mEGLManager->getContext())) {
        PIPELINE_LOGE("Failed to make EGL current: 0x%x", eglGetError());
        return false;
    }
    
    // 初始化渲染资源（首次）
    if (!mResourcesInitialized) {
        if (!initializeRenderResources()) {
            return false;
        }
    }
    
    // 清屏
    glClearColor(mDisplayConfig.backgroundColor[0],
                 mDisplayConfig.backgroundColor[1],
                 mDisplayConfig.backgroundColor[2],
                 mDisplayConfig.backgroundColor[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    
    mState = SurfaceState::Rendering;
    return true;
}

bool AndroidEGLSurface::renderTexture(std::shared_ptr<lrengine::render::LRTexture> texture,
                                       const DisplayConfig& config) {
    if (mState != SurfaceState::Rendering) {
        return false;
    }
    
    if (!texture) {
        return false;
    }
    
    // TODO: 从 LRTexture 获取 GL texture ID
    // GLuint textureId = texture->getGLTextureId();
    // return drawTextureToScreen(textureId, config);
    
    return true;
}

bool AndroidEGLSurface::endFrame() {
    if (mState != SurfaceState::Rendering) {
        return false;
    }
    
    // 交换缓冲区
    if (!eglSwapBuffers(mEGLDisplay, mEGLSurface)) {
        EGLint error = eglGetError();
        if (error == EGL_BAD_SURFACE) {
            PIPELINE_LOGW("EGL surface lost, need recreation");
            mState = SurfaceState::Error;
            return false;
        }
        PIPELINE_LOGE("eglSwapBuffers failed: 0x%x", error);
        return false;
    }
    
    mState = SurfaceState::Ready;
    return true;
}

void AndroidEGLSurface::waitGPU() {
    glFinish();
}

void AndroidEGLSurface::setVSyncEnabled(bool enabled) {
    DisplaySurface::setVSyncEnabled(enabled);
    
    if (mEGLDisplay != EGL_NO_DISPLAY) {
        eglSwapInterval(mEGLDisplay, enabled ? 1 : 0);
    }
}

// =============================================================================
// Android 特定接口
// =============================================================================

void AndroidEGLSurface::setEGLContextManager(AndroidEGLContextManager* manager) {
    mEGLManager = manager;
}

// =============================================================================
// 内部方法
// =============================================================================

bool AndroidEGLSurface::createEGLWindowSurface() {
    if (!mNativeWindow || mEGLDisplay == EGL_NO_DISPLAY) {
        return false;
    }
    
    // 选择 EGL 配置
    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };
    
    EGLint numConfigs;
    if (!eglChooseConfig(mEGLDisplay, configAttribs, &mEGLConfig, 1, &numConfigs) || 
        numConfigs == 0) {
        PIPELINE_LOGE("Failed to choose EGL config");
        return false;
    }
    
    // 设置窗口格式
    EGLint format;
    eglGetConfigAttrib(mEGLDisplay, mEGLConfig, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(mNativeWindow, 0, 0, format);
    
    // 创建 window surface
    mEGLSurface = eglCreateWindowSurface(mEGLDisplay, mEGLConfig, mNativeWindow, nullptr);
    if (mEGLSurface == EGL_NO_SURFACE) {
        PIPELINE_LOGE("Failed to create EGL window surface: 0x%x", eglGetError());
        return false;
    }
    
    // 设置 VSync
    eglSwapInterval(mEGLDisplay, mVSyncEnabled ? 1 : 0);
    
    PIPELINE_LOGI("EGL window surface created");
    return true;
}

void AndroidEGLSurface::destroyEGLSurface() {
    if (mEGLSurface != EGL_NO_SURFACE) {
        eglDestroySurface(mEGLDisplay, mEGLSurface);
        mEGLSurface = EGL_NO_SURFACE;
    }
}

bool AndroidEGLSurface::initializeRenderResources() {
    // 创建显示 shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &DISPLAY_VERTEX_SHADER, nullptr);
    glCompileShader(vertexShader);
    
    GLint success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(vertexShader, 512, nullptr, log);
        PIPELINE_LOGE("Display vertex shader error: %s", log);
        glDeleteShader(vertexShader);
        return false;
    }
    
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &DISPLAY_FRAGMENT_SHADER, nullptr);
    glCompileShader(fragmentShader);
    
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(fragmentShader, 512, nullptr, log);
        PIPELINE_LOGE("Display fragment shader error: %s", log);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }
    
    mDisplayShaderProgram = glCreateProgram();
    glAttachShader(mDisplayShaderProgram, vertexShader);
    glAttachShader(mDisplayShaderProgram, fragmentShader);
    glLinkProgram(mDisplayShaderProgram);
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    glGetProgramiv(mDisplayShaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(mDisplayShaderProgram, 512, nullptr, log);
        PIPELINE_LOGE("Display shader link error: %s", log);
        glDeleteProgram(mDisplayShaderProgram);
        mDisplayShaderProgram = 0;
        return false;
    }
    
    mTextureLocation = glGetUniformLocation(mDisplayShaderProgram, "uTexture");
    mTransformLocation = glGetUniformLocation(mDisplayShaderProgram, "uTransform");
    
    // 创建 VAO/VBO
    glGenVertexArrays(1, &mVAO);
    glGenBuffers(1, &mVBO);
    
    glBindVertexArray(mVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(SCREEN_QUAD), SCREEN_QUAD, GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 
                          (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindVertexArray(0);
    
    mResourcesInitialized = true;
    return true;
}

void AndroidEGLSurface::cleanupRenderResources() {
    if (mDisplayShaderProgram != 0) {
        glDeleteProgram(mDisplayShaderProgram);
        mDisplayShaderProgram = 0;
    }
    
    if (mVAO != 0) {
        glDeleteVertexArrays(1, &mVAO);
        mVAO = 0;
    }
    
    if (mVBO != 0) {
        glDeleteBuffers(1, &mVBO);
        mVBO = 0;
    }
    
    mResourcesInitialized = false;
}

bool AndroidEGLSurface::drawTextureToScreen(GLuint textureId, const DisplayConfig& config) {
    glViewport(0, 0, mWidth, mHeight);
    
    glUseProgram(mDisplayShaderProgram);
    
    // 绑定纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glUniform1i(mTextureLocation, 0);
    
    // 设置变换矩阵（根据 config 计算）
    float transform[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    
    // 处理翻转
    if (config.flipHorizontal) {
        transform[0] = -1;
    }
    if (config.flipVertical) {
        transform[5] = -1;
    }
    
    glUniformMatrix4fv(mTransformLocation, 1, GL_FALSE, transform);
    
    // 绘制
    glBindVertexArray(mVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    
    glUseProgram(0);
    
    return true;
}

} // namespace android
} // namespace output
} // namespace pipeline

#endif // __ANDROID__
