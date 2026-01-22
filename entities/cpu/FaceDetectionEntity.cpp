/**
 * @file FaceDetectionEntity.cpp
 * @brief 人脸检测Entity实现 - CPU端人脸检测算法
 */

#include "FaceDetectionEntity.h"
#include "pipeline/data/FramePacket.h"

#include <cmath>
#include <algorithm>
#include <cstring>

namespace pipeline {

// =============================================================================
// PIMPL实现类
// =============================================================================

class FaceDetectionEntity::Impl {
public:
    Impl() = default;
    ~Impl() = default;
    
    bool initialize(FaceDetectorBackend backend, const std::string& modelPath) {
        mBackend = backend;
        
        switch (backend) {
            case FaceDetectorBackend::OpenCV:
                return initOpenCV(modelPath);
            case FaceDetectorBackend::NCNN:
                return initNCNN(modelPath);
            case FaceDetectorBackend::TFLite:
                return initTFLite(modelPath);
            case FaceDetectorBackend::Custom:
                return true;
            default:
                return false;
        }
    }
    
    bool detect(const uint8_t* grayData,
               uint32_t width, uint32_t height,
               float minFaceSize, float threshold,
               uint32_t maxFaces,
               std::vector<FaceInfo>& faces) {
        
        switch (mBackend) {
            case FaceDetectorBackend::OpenCV:
                return detectOpenCV(grayData, width, height, minFaceSize, threshold, maxFaces, faces);
            case FaceDetectorBackend::NCNN:
                return detectNCNN(grayData, width, height, minFaceSize, threshold, maxFaces, faces);
            case FaceDetectorBackend::TFLite:
                return detectTFLite(grayData, width, height, minFaceSize, threshold, maxFaces, faces);
            default:
                return false;
        }
    }
    
    bool detectLandmarks(const uint8_t* grayData,
                        uint32_t width, uint32_t height,
                        FaceInfo& face,
                        uint32_t landmarkCount) {
        // TODO: 实现关键点检测
        return false;
    }
    
private:
    bool initOpenCV(const std::string& modelPath) {
        // TODO: 初始化OpenCV级联分类器
        // mCascade = cv::CascadeClassifier(modelPath);
        // return !mCascade.empty();
        mInitialized = true;
        return true;
    }
    
    bool initNCNN(const std::string& modelPath) {
        // TODO: 初始化NCNN网络
        // mNet.load_param(modelPath + ".param");
        // mNet.load_model(modelPath + ".bin");
        mInitialized = true;
        return true;
    }
    
    bool initTFLite(const std::string& modelPath) {
        // TODO: 初始化TFLite解释器
        // mModel = tflite::FlatBufferModel::BuildFromFile(modelPath.c_str());
        // tflite::InterpreterBuilder builder(*mModel, mResolver);
        // builder(&mInterpreter);
        mInitialized = true;
        return true;
    }
    
    bool detectOpenCV(const uint8_t* grayData,
                     uint32_t width, uint32_t height,
                     float minFaceSize, float threshold,
                     uint32_t maxFaces,
                     std::vector<FaceInfo>& faces) {
        // TODO: 使用OpenCV级联分类器检测
        // cv::Mat gray(height, width, CV_8UC1, (void*)grayData);
        // std::vector<cv::Rect> detectedFaces;
        // mCascade.detectMultiScale(gray, detectedFaces, 1.1, 3, 0,
        //     cv::Size(minFaceSize * std::min(width, height), 
        //              minFaceSize * std::min(width, height)));
        // 
        // for (const auto& rect : detectedFaces) {
        //     FaceInfo face;
        //     face.x = static_cast<float>(rect.x) / width;
        //     face.y = static_cast<float>(rect.y) / height;
        //     face.width = static_cast<float>(rect.width) / width;
        //     face.height = static_cast<float>(rect.height) / height;
        //     face.confidence = 1.0f;
        //     faces.push_back(face);
        //     if (faces.size() >= maxFaces) break;
        // }
        
        // 模拟检测结果（用于测试）
        // 在实际使用中，这里应该调用真实的检测算法
        return true;
    }
    
    bool detectNCNN(const uint8_t* grayData,
                   uint32_t width, uint32_t height,
                   float minFaceSize, float threshold,
                   uint32_t maxFaces,
                   std::vector<FaceInfo>& faces) {
        // TODO: 使用NCNN推理检测
        // ncnn::Mat in = ncnn::Mat::from_pixels(grayData, ncnn::Mat::PIXEL_GRAY, width, height);
        // ncnn::Extractor ex = mNet.create_extractor();
        // ex.input("data", in);
        // ncnn::Mat out;
        // ex.extract("output", out);
        // 
        // // 解析输出
        // for (int i = 0; i < out.h; i++) {
        //     const float* values = out.row(i);
        //     if (values[4] > threshold) {
        //         FaceInfo face;
        //         face.x = values[0];
        //         face.y = values[1];
        //         face.width = values[2] - values[0];
        //         face.height = values[3] - values[1];
        //         face.confidence = values[4];
        //         faces.push_back(face);
        //     }
        // }
        
        return true;
    }
    
    bool detectTFLite(const uint8_t* grayData,
                     uint32_t width, uint32_t height,
                     float minFaceSize, float threshold,
                     uint32_t maxFaces,
                     std::vector<FaceInfo>& faces) {
        // TODO: 使用TFLite推理检测
        // 填充输入张量
        // mInterpreter->Invoke();
        // 解析输出张量
        
        return true;
    }
    
private:
    FaceDetectorBackend mBackend = FaceDetectorBackend::OpenCV;
    bool mInitialized = false;
    
    // OpenCV相关
    // cv::CascadeClassifier mCascade;
    
    // NCNN相关
    // ncnn::Net mNet;
    
    // TFLite相关
    // std::unique_ptr<tflite::FlatBufferModel> mModel;
    // std::unique_ptr<tflite::Interpreter> mInterpreter;
    // tflite::ops::builtin::BuiltinOpResolver mResolver;
};

// =============================================================================
// FaceDetectionEntity 实现
// =============================================================================

FaceDetectionEntity::FaceDetectionEntity(const std::string& name)
    : CPUEntity(name)
    , mImpl(std::make_unique<Impl>()) {
    // 创建端口
    addInputPort("input");
    addOutputPort("output");
    
    // 设置降采样以提高性能
    setProcessingScale(0.5f);  // 默认以一半分辨率处理
}

FaceDetectionEntity::~FaceDetectionEntity() = default;

// =============================================================================
// 模型配置
// =============================================================================

void FaceDetectionEntity::setBackend(FaceDetectorBackend backend) {
    if (mBackend != backend) {
        mBackend = backend;
        mModelLoaded = false;
    }
}

bool FaceDetectionEntity::loadModel(const std::string& modelPath) {
    mModelPath = modelPath;
    mModelLoaded = mImpl->initialize(mBackend, modelPath);
    return mModelLoaded;
}

bool FaceDetectionEntity::loadLandmarkModel(const std::string& modelPath) {
    mLandmarkModelPath = modelPath;
    // TODO: 加载关键点模型
    return true;
}

// =============================================================================
// 检测参数
// =============================================================================

void FaceDetectionEntity::setMinFaceSize(float minSize) {
    mMinFaceSize = std::clamp(minSize, 0.01f, 1.0f);
}

void FaceDetectionEntity::setMaxFaces(uint32_t maxFaces) {
    mMaxFaces = std::max(1u, maxFaces);
}

void FaceDetectionEntity::setConfidenceThreshold(float threshold) {
    mConfidenceThreshold = std::clamp(threshold, 0.0f, 1.0f);
}

void FaceDetectionEntity::setDetectLandmarks(bool detect) {
    mDetectLandmarks = detect;
}

void FaceDetectionEntity::setLandmarkCount(uint32_t count) {
    // 常见的关键点数量：68, 98, 106
    if (count == 68 || count == 98 || count == 106 || count == 5) {
        mLandmarkCount = count;
    }
}

// =============================================================================
// 性能配置
// =============================================================================

void FaceDetectionEntity::setDetectionInterval(uint32_t interval) {
    mDetectionInterval = std::max(1u, interval);
}

void FaceDetectionEntity::setTrackingEnabled(bool enabled) {
    mTrackingEnabled = enabled;
}

// =============================================================================
// CPU处理
// =============================================================================

bool FaceDetectionEntity::processOnCPU(const uint8_t* data,
                                       uint32_t width,
                                       uint32_t height,
                                       uint32_t stride,
                                       PixelFormat format,
                                       std::unordered_map<std::string, std::any>& metadata) {
    if (!data || width == 0 || height == 0) {
        return false;
    }
    
    mFrameCounter++;
    
    // 决定是执行检测还是跟踪
    bool needDetection = (mFrameCounter % mDetectionInterval == 0) || 
                         mLastResult.faces.empty();
    
    std::vector<FaceInfo> faces;
    
    if (needDetection) {
        // 执行完整检测
        if (!detectFaces(data, width, height, stride, faces)) {
            // 检测失败，使用上次结果
            faces = mLastResult.faces;
        }
    } else if (mTrackingEnabled && !mLastResult.faces.empty()) {
        // 执行跟踪
        faces = mLastResult.faces;
        trackFaces(data, width, height, stride, faces);
    } else {
        // 复用上次结果
        faces = mLastResult.faces;
    }
    
    // 检测关键点
    if (mDetectLandmarks && !faces.empty()) {
        for (auto& face : faces) {
            detectLandmarks(data, width, height, stride, face);
        }
    }
    
    // 更新结果
    mLastResult.faces = faces;
    mLastResult.timestamp = 0; // 由调用方设置
    mLastResult.imageWidth = width;
    mLastResult.imageHeight = height;
    
    // 存储到metadata
    // 将结果序列化为可存储的格式
    if (!faces.empty()) {
        // 存储人脸数量
        metadata["face_count"] = static_cast<int>(faces.size());
        
        // 存储每个人脸的信息
        for (size_t i = 0; i < faces.size(); ++i) {
            const auto& face = faces[i];
            std::string prefix = "face_" + std::to_string(i) + "_";
            
            metadata[prefix + "x"] = face.x;
            metadata[prefix + "y"] = face.y;
            metadata[prefix + "w"] = face.width;
            metadata[prefix + "h"] = face.height;
            metadata[prefix + "confidence"] = face.confidence;
            metadata[prefix + "roll"] = face.roll;
            metadata[prefix + "pitch"] = face.pitch;
            metadata[prefix + "yaw"] = face.yaw;
            
            // 存储关键点
            if (!face.landmarks.empty()) {
                metadata[prefix + "landmark_count"] = static_cast<int>(face.landmarks.size());
                for (size_t j = 0; j < face.landmarks.size(); ++j) {
                    metadata[prefix + "lm_" + std::to_string(j) + "_x"] = face.landmarks[j].x;
                    metadata[prefix + "lm_" + std::to_string(j) + "_y"] = face.landmarks[j].y;
                }
            }
        }
        
        // 存储主要人脸的边界框（便于简单访问）
        std::string boundsStr = std::to_string(faces[0].x) + "," + 
                               std::to_string(faces[0].y) + "," +
                               std::to_string(faces[0].width) + "," +
                               std::to_string(faces[0].height);
        metadata["face_bounds"] = boundsStr;
        metadata["face_landmarks"] = boundsStr;  // 兼容BeautyEntity
    } else {
        metadata["face_count"] = 0;
    }
    
    return true;
}

// =============================================================================
// 检测实现
// =============================================================================

bool FaceDetectionEntity::detectFaces(const uint8_t* data,
                                      uint32_t width, uint32_t height,
                                      uint32_t stride,
                                      std::vector<FaceInfo>& faces) {
    // 转换为灰度图
    convertToGray(data, width, height, stride, mGrayBuffer);
    
    // 调用检测后端
    return mImpl->detect(mGrayBuffer.data(), width, height,
                        mMinFaceSize, mConfidenceThreshold, mMaxFaces, faces);
}

bool FaceDetectionEntity::detectLandmarks(const uint8_t* data,
                                          uint32_t width, uint32_t height,
                                          uint32_t stride,
                                          FaceInfo& face) {
    if (!mDetectLandmarks || mLandmarkModelPath.empty()) {
        return false;
    }
    
    // 确保灰度缓冲区存在
    if (mGrayBuffer.empty()) {
        convertToGray(data, width, height, stride, mGrayBuffer);
    }
    
    // 调用关键点检测
    return mImpl->detectLandmarks(mGrayBuffer.data(), width, height, face, mLandmarkCount);
}

bool FaceDetectionEntity::trackFaces(const uint8_t* data,
                                     uint32_t width, uint32_t height,
                                     uint32_t stride,
                                     std::vector<FaceInfo>& faces) {
    // 简单的跟踪实现：使用模板匹配或光流
    // 这里提供一个简化的实现，实际使用中应该用更robust的跟踪算法
    
    // 转换为灰度图
    convertToGray(data, width, height, stride, mGrayBuffer);
    
    // TODO: 实现真正的跟踪算法
    // 可以使用：
    // 1. KCF跟踪器
    // 2. 光流法
    // 3. 简单的模板匹配
    
    // 简化处理：假设人脸位置变化不大，直接返回
    // 在实际应用中，应该根据相邻帧的特征点匹配来更新位置
    
    return true;
}

void FaceDetectionEntity::convertToGray(const uint8_t* rgba,
                                        uint32_t width, uint32_t height,
                                        uint32_t stride,
                                        std::vector<uint8_t>& gray) {
    gray.resize(width * height);
    
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = rgba + y * stride;
        uint8_t* grayRow = gray.data() + y * width;
        
        for (uint32_t x = 0; x < width; ++x) {
            // RGBA格式，使用标准灰度转换公式
            // Gray = 0.299*R + 0.587*G + 0.114*B
            const uint8_t* pixel = row + x * 4;
            uint8_t r = pixel[0];
            uint8_t g = pixel[1];
            uint8_t b = pixel[2];
            
            // 使用整数运算近似：(77*R + 150*G + 29*B) >> 8
            grayRow[x] = static_cast<uint8_t>((77 * r + 150 * g + 29 * b) >> 8);
        }
    }
}

} // namespace pipeline
