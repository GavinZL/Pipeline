/**
 * @file FilterEntity.h
 * @brief LUT滤镜Entity - 使用查找表进行色彩校正
 */

#pragma once

#include "pipeline/entity/GPUEntity.h"

namespace pipeline {

/**
 * @brief LUT类型
 */
enum class LUTType : uint8_t {
    LUT1D,       // 1D LUT
    LUT3D,       // 3D LUT（最常用）
    ColorMatrix  // 颜色矩阵
};

/**
 * @brief LUT滤镜Entity
 * 
 * 使用查找表(Look-Up Table)进行色彩校正和滤镜效果。
 * 支持1D LUT、3D LUT和颜色矩阵。
 * 
 * 使用方式：
 * 1. 创建FilterEntity
 * 2. 加载LUT数据（从文件或内存）
 * 3. 设置强度
 */
class FilterEntity : public GPUEntity {
public:
    /**
     * @brief 构造函数
     * @param name Entity名称
     */
    explicit FilterEntity(const std::string& name = "FilterEntity");
    
    ~FilterEntity() override;
    
    // ==========================================================================
    // LUT加载
    // ==========================================================================
    
    /**
     * @brief 从文件加载3D LUT
     * 
     * 支持.cube和.3dl格式。
     * @param path LUT文件路径
     * @return 是否成功
     */
    bool loadLUTFromFile(const std::string& path);
    
    /**
     * @brief 从内存加载3D LUT
     * @param data LUT数据（RGB格式）
     * @param size LUT尺寸（如64表示64x64x64）
     * @return 是否成功
     */
    bool loadLUT3D(const uint8_t* data, uint32_t size);
    
    /**
     * @brief 从内存加载3D LUT（浮点格式）
     * @param data LUT数据（RGB浮点格式，0-1范围）
     * @param size LUT尺寸
     * @return 是否成功
     */
    bool loadLUT3DFloat(const float* data, uint32_t size);
    
    /**
     * @brief 设置颜色矩阵
     * @param matrix 4x4颜色矩阵
     */
    void setColorMatrix(const float matrix[16]);
    
    /**
     * @brief 设置预设滤镜
     * @param presetName 预设名称
     * @return 是否成功
     */
    bool setPreset(const std::string& presetName);
    
    // ==========================================================================
    // 参数配置
    // ==========================================================================
    
    /**
     * @brief 设置滤镜强度
     * @param intensity 强度（0-1，0表示原图，1表示完全应用滤镜）
     */
    void setIntensity(float intensity);
    
    /**
     * @brief 获取滤镜强度
     */
    float getIntensity() const { return mIntensity; }
    
    /**
     * @brief 设置亮度调整
     * @param brightness 亮度（-1到1，0为原值）
     */
    void setBrightness(float brightness);
    
    /**
     * @brief 设置对比度调整
     * @param contrast 对比度（0到2，1为原值）
     */
    void setContrast(float contrast);
    
    /**
     * @brief 设置饱和度调整
     * @param saturation 饱和度（0到2，1为原值）
     */
    void setSaturation(float saturation);
    
    /**
     * @brief 设置色温调整
     * @param temperature 色温（-1到1，0为原值）
     */
    void setTemperature(float temperature);
    
    /**
     * @brief 设置色调调整
     * @param tint 色调（-1到1，0为原值）
     */
    void setTint(float tint);
    
    /**
     * @brief 获取LUT类型
     */
    LUTType getLUTType() const { return mLUTType; }
    
    /**
     * @brief 获取LUT尺寸
     */
    uint32_t getLUTSize() const { return mLUTSize; }
    
protected:
    bool setupShader() override;
    void setUniforms(FramePacket* input) override;
    bool processGPU(const std::vector<FramePacketPtr>& inputs, 
                   FramePacketPtr output) override;
    
    void onParameterChanged(const std::string& key) override;
    
private:
    /**
     * @brief 创建LUT纹理
     */
    bool createLUTTexture();
    
    /**
     * @brief 解析.cube文件
     */
    bool parseCubeFile(const std::string& path);
    
    /**
     * @brief 生成颜色校正参数
     */
    void updateColorCorrection();
    
private:
    // LUT数据
    LUTType mLUTType = LUTType::LUT3D;
    uint32_t mLUTSize = 0;
    std::vector<float> mLUTData;
    std::shared_ptr<lrengine::render::LRTexture> mLUTTexture;
    bool mLUTNeedsUpdate = false;
    
    // 滤镜参数
    float mIntensity = 1.0f;
    float mBrightness = 0.0f;
    float mContrast = 1.0f;
    float mSaturation = 1.0f;
    float mTemperature = 0.0f;
    float mTint = 0.0f;
    
    // 颜色矩阵
    float mColorMatrix[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    
    // Uniform位置
    int32_t mIntensityLocation = -1;
    int32_t mBrightnessLocation = -1;
    int32_t mContrastLocation = -1;
    int32_t mSaturationLocation = -1;
    int32_t mColorMatrixLocation = -1;
    int32_t mLUTSizeLocation = -1;
};

} // namespace pipeline
