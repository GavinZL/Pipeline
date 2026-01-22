/**
 * @file CompositeEntity.h
 * @brief 合成节点 - 多输入混合处理
 */

#pragma once

#include "GPUEntity.h"

namespace pipeline {

/**
 * @brief 混合模式
 */
enum class BlendMode : uint8_t {
    Normal,      // 正常（Alpha混合）
    Add,         // 叠加
    Multiply,    // 正片叠底
    Screen,      // 滤色
    Overlay,     // 叠加
    SoftLight,   // 柔光
    HardLight,   // 强光
    Difference,  // 差值
    Exclusion    // 排除
};

/**
 * @brief 合成布局
 */
enum class CompositeLayout : uint8_t {
    Blend,           // 混合（两层叠加）
    SplitHorizontal, // 水平分屏
    SplitVertical,   // 垂直分屏
    Grid2x2,         // 2x2网格
    PictureInPicture // 画中画
};

/**
 * @brief 画中画配置
 */
struct PipConfig {
    float x = 0.7f;       // 小窗口X位置（0-1）
    float y = 0.7f;       // 小窗口Y位置（0-1）
    float width = 0.25f;  // 小窗口宽度比例（0-1）
    float height = 0.25f; // 小窗口高度比例（0-1）
    float cornerRadius = 0.0f; // 圆角半径
    float borderWidth = 0.0f;  // 边框宽度
    float borderColor[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // 边框颜色
};

/**
 * @brief 合成节点
 * 
 * 负责多路输入的混合、叠加、画中画等操作。
 * 
 * 特点：
 * - 支持2-8路输入
 * - 多种混合模式
 * - 多种布局模式
 * - GPU加速合成
 * 
 * 使用方式：
 * 1. 添加多个输入端口
 * 2. 设置混合模式和布局
 * 3. 配置各输入的参数（位置、大小、透明度等）
 */
class CompositeEntity : public GPUEntity {
public:
    /**
     * @brief 构造函数
     * @param name Entity名称
     * @param inputCount 输入数量（默认2）
     */
    explicit CompositeEntity(const std::string& name = "CompositeEntity", 
                            size_t inputCount = 2);
    
    ~CompositeEntity() override;
    
    // ==========================================================================
    // 类型信息
    // ==========================================================================
    
    EntityType getType() const override { return EntityType::Composite; }
    
    // ==========================================================================
    // 混合配置
    // ==========================================================================
    
    /**
     * @brief 设置混合模式
     */
    void setBlendMode(BlendMode mode);
    
    /**
     * @brief 获取混合模式
     */
    BlendMode getBlendMode() const { return mBlendMode; }
    
    /**
     * @brief 设置布局模式
     */
    void setLayout(CompositeLayout layout);
    
    /**
     * @brief 获取布局模式
     */
    CompositeLayout getLayout() const { return mLayout; }
    
    /**
     * @brief 设置画中画配置
     */
    void setPipConfig(const PipConfig& config);
    
    /**
     * @brief 获取画中画配置
     */
    const PipConfig& getPipConfig() const { return mPipConfig; }
    
    // ==========================================================================
    // 输入配置
    // ==========================================================================
    
    /**
     * @brief 设置输入透明度
     * @param inputIndex 输入索引
     * @param alpha 透明度（0-1）
     */
    void setInputAlpha(size_t inputIndex, float alpha);
    
    /**
     * @brief 获取输入透明度
     */
    float getInputAlpha(size_t inputIndex) const;
    
    /**
     * @brief 设置输入变换矩阵
     * @param inputIndex 输入索引
     * @param transform 4x4变换矩阵
     */
    void setInputTransform(size_t inputIndex, const float* transform);
    
    /**
     * @brief 设置输入可见性
     * @param inputIndex 输入索引
     * @param visible 是否可见
     */
    void setInputVisible(size_t inputIndex, bool visible);
    
    /**
     * @brief 获取输入可见性
     */
    bool isInputVisible(size_t inputIndex) const;
    
    /**
     * @brief 设置输入顺序（Z-Order）
     * @param inputIndex 输入索引
     * @param zOrder Z顺序（越大越在上层）
     */
    void setInputZOrder(size_t inputIndex, int32_t zOrder);
    
    // ==========================================================================
    // 输入管理
    // ==========================================================================
    
    /**
     * @brief 添加输入
     * @return 新输入的索引
     */
    size_t addInput();
    
    /**
     * @brief 获取输入数量
     */
    size_t getInputCount() const { return mInputConfigs.size(); }
    
    /**
     * @brief 设置是否需要所有输入就绪
     * 
     * 如果为false，缺失的输入将使用透明填充。
     * 默认为false。
     */
    void setRequireAllInputs(bool require) { mRequireAllInputs = require; }
    
protected:
    // ==========================================================================
    // 实现
    // ==========================================================================
    
    bool setupShader() override;
    void setUniforms(FramePacket* input) override;
    bool processGPU(const std::vector<FramePacketPtr>& inputs, 
                   FramePacketPtr output) override;
    
    /**
     * @brief 生成混合着色器代码
     */
    std::string generateBlendShader() const;
    
    /**
     * @brief 计算输入的UV变换
     */
    void calculateUVTransforms();
    
protected:
    /**
     * @brief 单个输入的配置
     */
    struct InputConfig {
        float alpha = 1.0f;
        bool visible = true;
        int32_t zOrder = 0;
        float transform[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        float uvTransform[4] = {0, 0, 1, 1}; // x, y, width, height in UV space
    };
    
    // 混合配置
    BlendMode mBlendMode = BlendMode::Normal;
    CompositeLayout mLayout = CompositeLayout::Blend;
    PipConfig mPipConfig;
    
    // 输入配置
    std::vector<InputConfig> mInputConfigs;
    bool mRequireAllInputs = false;
    
    // Uniform位置缓存
    int32_t mBlendModeLocation = -1;
    int32_t mInputCountLocation = -1;
    std::vector<int32_t> mInputAlphaLocations;
    std::vector<int32_t> mInputUVTransformLocations;

    bool mNeedsShaderUpdate = false;
};

} // namespace pipeline
