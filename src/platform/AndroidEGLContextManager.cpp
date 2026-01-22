/**
 * @file AndroidEGLContextManager.cpp
 * @brief Android EGL 上下文管理器实现
 */

#ifdef __ANDROID__

#include "pipeline/platform/PlatformContext.h"
#include <android/log.h>

#define LOG_TAG "AndroidEGLContextManager"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace pipeline {

// =============================================================================
// AndroidEGLContextManager 实现
// =============================================================================

AndroidEGLContextManager::AndroidEGLContextManager()
    : mDisplay(EGL_NO_DISPLAY)
    , mContext(EGL_NO_CONTEXT)
    , mSurface(EGL_NO_SURFACE)
    , mConfig(nullptr)
    , mInitialized(false)
{
}

AndroidEGLContextManager::~AndroidEGLContextManager() {
    destroy();
}

bool AndroidEGLContextManager::initialize(const Config& config) {
    std::lock_guard<std::mutex> lock(mMutex);
    
    if (mInitialized) {
        LOGD("Already initialized");
        return true;
    }
    
    LOGI("Initializing AndroidEGLContextManager");
    
    // 1. 获取 EGL Display
    if (config.display != EGL_NO_DISPLAY) {
        mDisplay = config.display;
        LOGD("Using provided EGL display: %p", mDisplay);
    } else {
        mDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (mDisplay == EGL_NO_DISPLAY) {
            LOGE("eglGetDisplay failed");
            return false;
        }
        LOGD("Created new EGL display: %p", mDisplay);
        
        // 初始化 EGL
        EGLint major, minor;
        if (!eglInitialize(mDisplay, &major, &minor)) {
            LOGE("eglInitialize failed: 0x%x", eglGetError());
            return false;
        }
        LOGI("EGL initialized: version %d.%d", major, minor);
    }
    
    // 2. 选择配置
    EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, config.offscreen ? EGL_PBUFFER_BIT : EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };
    
    EGLint numConfigs;
    if (!eglChooseConfig(mDisplay, configAttribs, &mConfig, 1, &numConfigs) || numConfigs == 0) {
        LOGE("eglChooseConfig failed: 0x%x", eglGetError());
        return false;
    }
    LOGD("Found %d matching configs", numConfigs);
    
    // 3. 创建 Surface
    if (config.offscreen) {
        // 创建 PBuffer Surface（离屏渲染）
        EGLint pbufferAttribs[] = {
            EGL_WIDTH, config.pbufferWidth,
            EGL_HEIGHT, config.pbufferHeight,
            EGL_NONE
        };
        
        mSurface = eglCreatePbufferSurface(mDisplay, mConfig, pbufferAttribs);
        if (mSurface == EGL_NO_SURFACE) {
            LOGE("eglCreatePbufferSurface failed: 0x%x", eglGetError());
            return false;
        }
        LOGD("Created PBuffer surface: %dx%d", config.pbufferWidth, config.pbufferHeight);
    } else {
        // 使用 1x1 PBuffer 作为默认 Surface
        EGLint pbufferAttribs[] = {
            EGL_WIDTH, 1,
            EGL_HEIGHT, 1,
            EGL_NONE
        };
        
        mSurface = eglCreatePbufferSurface(mDisplay, mConfig, pbufferAttribs);
        if (mSurface == EGL_NO_SURFACE) {
            LOGE("eglCreatePbufferSurface failed: 0x%x", eglGetError());
            return false;
        }
        LOGD("Created default 1x1 PBuffer surface");
    }
    
    // 4. 创建 EGL Context
    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, config.glesVersion,
        EGL_NONE
    };
    
    EGLContext shareContext = config.sharedContext != EGL_NO_CONTEXT 
                            ? config.sharedContext 
                            : EGL_NO_CONTEXT;
    
    mContext = eglCreateContext(mDisplay, mConfig, shareContext, contextAttribs);
    if (mContext == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed: 0x%x", eglGetError());
        destroy();
        return false;
    }
    
    if (shareContext != EGL_NO_CONTEXT) {
        LOGI("Created EGL context with sharing from: %p", shareContext);
    } else {
        LOGI("Created standalone EGL context");
    }
    
    // 5. 激活上下文以验证
    if (!eglMakeCurrent(mDisplay, mSurface, mSurface, mContext)) {
        LOGE("eglMakeCurrent failed: 0x%x", eglGetError());
        destroy();
        return false;
    }
    
    // 打印 GL 信息
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version = (const char*)glGetString(GL_VERSION);
    LOGI("GL Vendor: %s", vendor ? vendor : "unknown");
    LOGI("GL Renderer: %s", renderer ? renderer : "unknown");
    LOGI("GL Version: %s", version ? version : "unknown");
    
    mInitialized = true;
    LOGI("AndroidEGLContextManager initialized successfully");
    
    return true;
}

EGLContext AndroidEGLContextManager::createSharedContext(EGLContext sourceContext) {
    std::lock_guard<std::mutex> lock(mMutex);
    
    if (!mInitialized) {
        LOGE("Not initialized");
        return EGL_NO_CONTEXT;
    }
    
    if (sourceContext == EGL_NO_CONTEXT) {
        sourceContext = mContext;
    }
    
    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    
    EGLContext sharedContext = eglCreateContext(mDisplay, mConfig, sourceContext, contextAttribs);
    if (sharedContext == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext (shared) failed: 0x%x", eglGetError());
        return EGL_NO_CONTEXT;
    }
    
    LOGI("Created shared context: %p from source: %p", sharedContext, sourceContext);
    
    return sharedContext;
}

bool AndroidEGLContextManager::makeCurrent() {
    if (!mInitialized) {
        LOGE("Not initialized");
        return false;
    }
    
    if (!eglMakeCurrent(mDisplay, mSurface, mSurface, mContext)) {
        LOGE("eglMakeCurrent failed: 0x%x", eglGetError());
        return false;
    }
    
    return true;
}

bool AndroidEGLContextManager::releaseCurrent() {
    if (!mInitialized) {
        return false;
    }
    
    if (!eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
        LOGE("eglMakeCurrent (release) failed: 0x%x", eglGetError());
        return false;
    }
    
    return true;
}

bool AndroidEGLContextManager::isCurrent() const {
    if (!mInitialized) {
        return false;
    }
    
    return eglGetCurrentContext() == mContext;
}

void AndroidEGLContextManager::destroy() {
    std::lock_guard<std::mutex> lock(mMutex);
    
    if (!mInitialized) {
        return;
    }
    
    LOGI("Destroying AndroidEGLContextManager");
    
    // 释放当前上下文
    if (mDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        
        if (mContext != EGL_NO_CONTEXT) {
            eglDestroyContext(mDisplay, mContext);
            mContext = EGL_NO_CONTEXT;
        }
        
        if (mSurface != EGL_NO_SURFACE) {
            eglDestroySurface(mDisplay, mSurface);
            mSurface = EGL_NO_SURFACE;
        }
        
        // 注意：如果 display 是外部提供的，不要终止它
        // eglTerminate(mDisplay);
        // mDisplay = EGL_NO_DISPLAY;
    }
    
    mInitialized = false;
    LOGI("AndroidEGLContextManager destroyed");
}

} // namespace pipeline

#endif // __ANDROID__
