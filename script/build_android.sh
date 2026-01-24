#!/bin/bash

################################################################################
# Pipeline Android 构建脚本
# 
# 功能：
# - 为Android平台交叉编译Pipeline库及其依赖（LREngine、TaskQueue）
# - 生成共享库（.so）或静态库（.a）
# - 支持多种ABI架构（armeabi-v7a, arm64-v8a, x86, x86_64）
# - 支持Debug和Release配置
# - 自动配置OpenGL ES后端
#
# 使用方法：
#   ./build_android.sh [选项]
#
# 选项：
#   -c, --config <Debug|Release>  构建配置（默认：Release）
#   -a, --abi <架构>              目标架构（默认：arm64-v8a）
#   -o, --output <路径>           输出目录（默认：./build/android）
#   -l, --api-level <级别>        Android API级别（默认：21）
#   -s, --static                  生成静态库而非共享库
#   -h, --help                    显示帮助信息
#
# 示例：
#   ./build_android.sh                          # 构建arm64的Release版本
#   ./build_android.sh -c Debug                 # 构建Debug版本
#   ./build_android.sh -a all                   # 构建所有架构
#   ./build_android.sh -a "arm64-v8a armeabi-v7a"  # 构建指定多个架构
################################################################################

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 默认参数
BUILD_CONFIG="Release"
TARGET_ABIS="arm64-v8a"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="${PROJECT_ROOT}/build/android"
LIBRARY_NAME="Pipeline"
VERSION="1.0.0"
BUILD_STATIC=false

# Android配置 - 自动检测
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Library/Android/sdk}}"
ANDROID_API_LEVEL="21"

# 支持的所有ABI架构
ALL_ABIS="armeabi-v7a arm64-v8a x86 x86_64"

# 第三方库路径
LRENGINE_DIR="${PROJECT_ROOT}/third_party/LREngine"
TASKQUEUE_DIR="${PROJECT_ROOT}/third_party/TaskQueue"

################################################################################
# 辅助函数
################################################################################

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_header() {
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}$1${NC}"
    echo -e "${GREEN}========================================${NC}"
}

print_subheader() {
    echo ""
    echo -e "${CYAN}----------------------------------------${NC}"
    echo -e "${CYAN}$1${NC}"
    echo -e "${CYAN}----------------------------------------${NC}"
}

show_help() {
    cat << EOF
Pipeline Android 构建脚本

用法: $0 [选项]

选项:
  -c, --config <Debug|Release>        构建配置（默认：Release）
  -a, --abi <架构>                    目标架构（默认：arm64-v8a）
  -o, --output <路径>                 输出目录（默认：./build/android）
  -l, --api-level <级别>              Android API级别（默认：21）
  -s, --static                        生成静态库（.a）而非共享库（.so）
  -h, --help                          显示此帮助信息

支持的ABI架构:
  armeabi-v7a   - 32位ARM（ARMv7）
  arm64-v8a     - 64位ARM（ARMv8/AArch64）
  x86           - 32位Intel x86
  x86_64        - 64位Intel x86_64
  all           - 构建所有架构

构建配置说明:
  Debug     - 包含调试符号，未优化，启用调试日志
  Release   - 优化构建，移除调试符号

示例:
  $0                                  # 构建arm64-v8a的Release版本
  $0 -c Debug                         # 构建Debug版本
  $0 -a all                           # 构建所有架构
  $0 -a "arm64-v8a armeabi-v7a"       # 构建64位和32位ARM架构
  $0 -l 24                            # 使用API Level 24
  $0 -s                               # 生成静态库
  $0 -o ~/output/android              # 输出到指定目录

输出文件结构:
  <输出目录>/
    ├── jniLibs/
    │   ├── armeabi-v7a/
    │   │   ├── libPipeline.so
    │   │   ├── liblrengine.so
    │   │   └── libc++_shared.so
    │   └── arm64-v8a/
    │       ├── libPipeline.so
    │       ├── liblrengine.so
    │       └── libc++_shared.so
    ├── include/
    │   ├── pipeline/                 # Pipeline头文件
    │   ├── lrengine/                 # LREngine头文件
    │   └── TaskQueue/                # TaskQueue头文件
    └── README.md                     # 使用说明

环境要求:
  - Android SDK
  - Android NDK（通过SDK安装）
  - CMake（3.15+）

EOF
}

################################################################################
# 参数解析
################################################################################

while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--config)
            BUILD_CONFIG="$2"
            shift 2
            ;;
        -a|--abi)
            TARGET_ABIS="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -l|--api-level)
            ANDROID_API_LEVEL="$2"
            shift 2
            ;;
        -s|--static)
            BUILD_STATIC=true
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            print_error "未知选项: $1"
            show_help
            exit 1
            ;;
    esac
done

# 验证构建配置
if [[ "$BUILD_CONFIG" != "Debug" && "$BUILD_CONFIG" != "Release" ]]; then
    print_error "无效的构建配置: $BUILD_CONFIG (必须是 Debug 或 Release)"
    exit 1
fi

# 处理目标架构
if [[ "$TARGET_ABIS" == "all" ]]; then
    TARGET_ABIS="$ALL_ABIS"
fi

# 验证架构参数
for abi in $TARGET_ABIS; do
    if [[ ! " $ALL_ABIS " =~ " $abi " ]]; then
        print_error "无效的ABI架构: $abi"
        print_info "支持的架构: $ALL_ABIS"
        exit 1
    fi
done

################################################################################
# 环境检查
################################################################################

check_environment() {
    print_header "检查构建环境"
    
    # 检查CMake
    if ! command -v cmake &> /dev/null; then
        print_error "未找到CMake，请先安装CMake"
        print_info "可以使用包管理器安装: brew install cmake (macOS) 或 apt install cmake (Linux)"
        exit 1
    fi
    CMAKE_VERSION=$(cmake --version | head -n1)
    print_success "CMake 版本: $CMAKE_VERSION"
    
    # 检查make或ninja
    if command -v ninja &> /dev/null; then
        BUILD_TOOL="Ninja"
        print_success "构建工具: Ninja"
    elif command -v make &> /dev/null; then
        BUILD_TOOL="Unix Makefiles"
        print_success "构建工具: Make"
    else
        print_error "未找到构建工具（Ninja或Make）"
        exit 1
    fi
    
    # 检查Android SDK
    if [[ ! -d "$ANDROID_SDK_ROOT" ]]; then
        print_error "未找到Android SDK: $ANDROID_SDK_ROOT"
        print_info "请设置 ANDROID_SDK_ROOT 或 ANDROID_HOME 环境变量"
        exit 1
    fi
    print_success "Android SDK: $ANDROID_SDK_ROOT"
    
    # 查找Android NDK
    if [[ -d "$ANDROID_SDK_ROOT/ndk-bundle" ]]; then
        ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk-bundle"
    elif [[ -d "$ANDROID_SDK_ROOT/ndk" ]]; then
        ANDROID_NDK_ROOT=$(ls -d "$ANDROID_SDK_ROOT/ndk"/*/ 2>/dev/null | sort -V | tail -n1 | sed 's:/$::')
    fi
    
    if [[ -z "$ANDROID_NDK_ROOT" || ! -d "$ANDROID_NDK_ROOT" ]]; then
        print_error "未找到Android NDK"
        print_info "请通过Android SDK Manager安装NDK:"
        print_info "  sdkmanager --install 'ndk;25.2.9519653'"
        exit 1
    fi
    print_success "Android NDK: $ANDROID_NDK_ROOT"
    
    # 获取NDK版本
    if [[ -f "$ANDROID_NDK_ROOT/source.properties" ]]; then
        NDK_VERSION=$(grep "Pkg.Revision" "$ANDROID_NDK_ROOT/source.properties" | cut -d'=' -f2 | tr -d ' ')
        print_info "NDK 版本: $NDK_VERSION"
    fi
    
    # 查找CMake工具链文件
    CMAKE_TOOLCHAIN="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake"
    if [[ ! -f "$CMAKE_TOOLCHAIN" ]]; then
        print_error "未找到Android CMake工具链文件"
        print_info "预期路径: $CMAKE_TOOLCHAIN"
        exit 1
    fi
    print_success "CMake工具链: $CMAKE_TOOLCHAIN"
    
    # 检查第三方库
    if [[ ! -d "$LRENGINE_DIR" ]]; then
        print_error "未找到LREngine: $LRENGINE_DIR"
        exit 1
    fi
    print_success "LREngine: $LRENGINE_DIR"
    
    if [[ ! -d "$TASKQUEUE_DIR" ]]; then
        print_error "未找到TaskQueue: $TASKQUEUE_DIR"
        exit 1
    fi
    print_success "TaskQueue: $TASKQUEUE_DIR"
    
    echo ""
}

################################################################################
# 清理函数
################################################################################

clean_build_dir() {
    local build_dir=$1
    if [[ -d "$build_dir" ]]; then
        print_info "清理旧的构建目录: $build_dir"
        rm -rf "$build_dir"
    fi
}

################################################################################
# 构建单个架构
################################################################################

build_for_abi() {
    local abi=$1
    
    print_subheader "构建 $abi 架构 (${BUILD_CONFIG})"
    
    local build_dir="${OUTPUT_DIR}/build_${abi}_${BUILD_CONFIG}"
    clean_build_dir "$build_dir"
    mkdir -p "$build_dir"
    
    # 确定库类型
    if [[ "$BUILD_STATIC" == true ]]; then
        LIB_TYPE="STATIC"
        LIB_EXTENSION="a"
        BUILD_SHARED="OFF"
    else
        LIB_TYPE="SHARED"
        LIB_EXTENSION="so"
        BUILD_SHARED="ON"
    fi
    
    print_info "配置CMake..."
    
    # 设置CMake参数
    local cmake_args=(
        -S "$PROJECT_ROOT"
        -B "$build_dir"
        -G "$BUILD_TOOL"
        -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN"
        -DANDROID_ABI="$abi"
        -DANDROID_PLATFORM="android-${ANDROID_API_LEVEL}"
        -DANDROID_NDK="$ANDROID_NDK_ROOT"
        -DANDROID_STL=c++_shared
        -DCMAKE_BUILD_TYPE="$BUILD_CONFIG"
        -DCMAKE_ANDROID_ARCH_ABI="$abi"
        -DPIPELINE_PLATFORM_ANDROID=ON
        -DPIPELINE_ENABLE_OPENGLES=ON
        -DPIPELINE_BUILD_TESTS=OFF
        -DPIPELINE_BUILD_EXAMPLES=OFF
        -DBUILD_SHARED_LIBS=${BUILD_SHARED}
    )
    
    # 针对不同架构的优化
    case "$abi" in
        armeabi-v7a)
            cmake_args+=(-DANDROID_ARM_NEON=ON)
            ;;
    esac
    
    # 执行CMake配置
    cmake "${cmake_args[@]}"
    
    if [[ $? -ne 0 ]]; then
        print_error "CMake配置失败: $abi"
        return 1
    fi
    
    print_info "编译Pipeline库..."
    local num_jobs=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    cmake --build "$build_dir" --config "$BUILD_CONFIG" --target Pipeline -- -j${num_jobs}
    
    if [[ $? -ne 0 ]]; then
        print_error "编译失败: $abi"
        return 1
    fi
    
    # 复制库文件到输出目录
    local output_lib_dir="${OUTPUT_DIR}/jniLibs/${abi}"
    mkdir -p "$output_lib_dir"
    
    # 查找并复制Pipeline库
    local pipeline_lib=$(find "$build_dir" -name "libPipeline.${LIB_EXTENSION}" -type f | head -n1)
    if [[ -f "$pipeline_lib" ]]; then
        cp "$pipeline_lib" "$output_lib_dir/"
        print_success "Pipeline库: $output_lib_dir/libPipeline.${LIB_EXTENSION}"
        print_info "  大小: $(du -h "$output_lib_dir/libPipeline.${LIB_EXTENSION}" | awk '{print $1}')"
    else
        print_error "未找到Pipeline库文件"
        return 1
    fi
    
    # 查找并复制LREngine库
    local lrengine_lib=$(find "$build_dir" -name "liblrengine.${LIB_EXTENSION}" -type f | head -n1)
    if [[ -f "$lrengine_lib" ]]; then
        cp "$lrengine_lib" "$output_lib_dir/"
        print_info "LREngine库: $(du -h "$output_lib_dir/liblrengine.${LIB_EXTENSION}" | awk '{print $1}')"
    fi
    
    # 查找并复制TaskQueue库
    local taskqueue_lib=$(find "$build_dir" -name "libTaskQueue.${LIB_EXTENSION}" -type f | head -n1)
    if [[ -f "$taskqueue_lib" ]]; then
        cp "$taskqueue_lib" "$output_lib_dir/"
        print_info "TaskQueue库: $(du -h "$output_lib_dir/libTaskQueue.${LIB_EXTENSION}" | awk '{print $1}')"
    fi
    
    # 如果是共享库，复制STL库
    if [[ "$BUILD_STATIC" == false ]]; then
        local stl_lib="${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/*/sysroot/usr/lib/${abi}/libc++_shared.so"
        stl_lib=$(ls $stl_lib 2>/dev/null | head -n1)
        if [[ -f "$stl_lib" ]]; then
            cp "$stl_lib" "$output_lib_dir/"
            print_info "已复制STL库: libc++_shared.so"
        fi
    fi
    
    echo ""
    return 0
}

################################################################################
# 复制头文件
################################################################################

copy_headers() {
    print_info "复制公共头文件..."
    
    local include_dir="${OUTPUT_DIR}/include"
    rm -rf "$include_dir"
    mkdir -p "$include_dir/pipeline"
    mkdir -p "$include_dir/lrengine"
    mkdir -p "$include_dir/TaskQueue"
    
    # Pipeline 头文件
    cp -R "$PROJECT_ROOT/include/pipeline/"* "$include_dir/pipeline/"
    
    # LREngine 头文件
    cp -R "$LRENGINE_DIR/include/lrengine/"* "$include_dir/lrengine/"
    
    # TaskQueue 头文件
    if [[ -d "$TASKQUEUE_DIR/TaskQueue" ]]; then
        find "$TASKQUEUE_DIR/TaskQueue" -name "*.h" -exec cp {} "$include_dir/TaskQueue/" \;
    fi
    
    print_success "头文件已复制到: $include_dir"
}

################################################################################
# 生成使用说明
################################################################################

generate_readme() {
    local readme_file="${OUTPUT_DIR}/README.md"
    
    cat > "$readme_file" << EOF
# Pipeline Android 库

Pipeline 是一个用于相机数据处理的跨平台框架。

## 构建信息

- 构建日期: $(date '+%Y-%m-%d %H:%M:%S')
- 构建配置: ${BUILD_CONFIG}
- 目标架构: ${TARGET_ABIS}
- Android API Level: ${ANDROID_API_LEVEL}
- 库类型: $([[ "$BUILD_STATIC" == true ]] && echo "静态库(.a)" || echo "共享库(.so)")
- NDK版本: ${NDK_VERSION:-未知}

## 包含内容

\`\`\`
android/
├── jniLibs/
│   ├── armeabi-v7a/
│   │   ├── libPipeline.so
│   │   ├── liblrengine.so
│   │   └── libc++_shared.so
│   └── arm64-v8a/
│       ├── libPipeline.so
│       ├── liblrengine.so
│       └── libc++_shared.so
└── include/
    ├── pipeline/             # Pipeline 头文件
    ├── lrengine/             # LREngine 头文件
    └── TaskQueue/            # TaskQueue 头文件
\`\`\`

## 集成方式

### 1. 复制库文件

将 \`jniLibs\` 目录复制到您的Android项目：
\`\`\`
app/src/main/jniLibs/
\`\`\`

### 2. 配置 build.gradle

\`\`\`groovy
android {
    defaultConfig {
        ndk {
            abiFilters 'armeabi-v7a', 'arm64-v8a'
        }
    }
    
    sourceSets {
        main {
            jniLibs.srcDirs = ['src/main/jniLibs']
        }
    }
}
\`\`\`

### 3. CMake 集成（如果使用NDK开发）

\`\`\`cmake
# 添加Pipeline预编译库
add_library(Pipeline SHARED IMPORTED)
set_target_properties(Pipeline PROPERTIES
    IMPORTED_LOCATION \${CMAKE_SOURCE_DIR}/../jniLibs/\${ANDROID_ABI}/libPipeline.so
)

# 链接
target_link_libraries(your_lib
    Pipeline
    GLESv3
    EGL
    android
    log
)
\`\`\`

### 4. 加载库

\`\`\`kotlin
companion object {
    init {
        System.loadLibrary("c++_shared")
        System.loadLibrary("lrengine")
        System.loadLibrary("Pipeline")
    }
}
\`\`\`

## 系统要求

- Android API Level ${ANDROID_API_LEVEL}+
- 支持架构: ${TARGET_ABIS}
- OpenGL ES 3.0+

EOF
    
    print_success "README已生成: $readme_file"
}

################################################################################
# 构建摘要
################################################################################

print_build_summary() {
    print_header "构建摘要"
    
    echo "输出目录: $OUTPUT_DIR"
    echo ""
    
    local built_count=0
    
    for abi in $TARGET_ABIS; do
        local lib_file="${OUTPUT_DIR}/jniLibs/${abi}/libPipeline.${LIB_EXTENSION}"
        if [[ -f "$lib_file" ]]; then
            local size=$(du -h "$lib_file" | awk '{print $1}')
            echo "  $abi: $size"
            ((built_count++))
        else
            echo "  $abi: 未构建"
        fi
    done
    
    echo ""
    
    if [[ -d "${OUTPUT_DIR}/jniLibs" ]]; then
        local total=$(du -sh "${OUTPUT_DIR}/jniLibs" | awk '{print $1}')
        echo "总大小: $total"
    fi
}

################################################################################
# 主函数
################################################################################

main() {
    print_header "Pipeline Android 构建脚本"
    
    echo "项目路径: $PROJECT_ROOT"
    echo "输出目录: $OUTPUT_DIR"
    echo "构建配置: $BUILD_CONFIG"
    echo "目标架构: $TARGET_ABIS"
    echo "API Level: $ANDROID_API_LEVEL"
    echo "库类型: $([[ "$BUILD_STATIC" == true ]] && echo "静态库" || echo "共享库")"
    echo ""
    
    # 检查环境
    check_environment
    
    # 创建输出目录
    mkdir -p "$OUTPUT_DIR"
    
    # 设置库类型变量
    if [[ "$BUILD_STATIC" == true ]]; then
        LIB_EXTENSION="a"
    else
        LIB_EXTENSION="so"
    fi
    
    # 构建各架构
    local failed_abis=""
    for abi in $TARGET_ABIS; do
        if ! build_for_abi "$abi"; then
            failed_abis+="$abi "
        fi
    done
    
    # 检查是否有失败
    if [[ -n "$failed_abis" ]]; then
        print_warning "以下架构构建失败: $failed_abis"
    fi
    
    # 复制头文件
    copy_headers
    
    # 生成使用说明
    generate_readme
    
    # 打印构建摘要
    print_build_summary
    
    # 完成
    print_header "构建完成！"
    
    echo "输出位置:"
    echo "  库文件: ${OUTPUT_DIR}/jniLibs/"
    echo "  头文件: ${OUTPUT_DIR}/include/"
    echo "  使用说明: ${OUTPUT_DIR}/README.md"
    echo ""
    
    if [[ -z "$failed_abis" ]]; then
        print_success "所有任务已完成！"
    else
        print_warning "部分架构构建失败，请检查错误信息"
    fi
}

# 执行主函数
main
