# GD32 MCU Zephyr 开发入门指南

本文档介绍如何基于个人定制的 Zephyr 仓库进行 GD32 MCU 的开发环境搭建和配置。

## 前置条件

- Linux 操作系统（推荐 Ubuntu 22.04 或更高版本）
- 网络连接正常
- 具有 sudo 权限

## 1. 安装系统依赖

首先安装 Zephyr 开发所需的系统依赖包：

```bash
sudo apt install --no-install-recommends git cmake ninja-build gperf \
  ccache dfu-util device-tree-compiler wget python3-dev python3-venv python3-tk \
  xz-utils file make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1
```

### 依赖包说明：
- **git**: 版本控制工具
- **cmake**: 构建系统
- **ninja-build**: 快速构建工具
- **gperf**: 完美哈希函数生成器
- **ccache**: 编译缓存工具
- **dfu-util**: DFU 固件烧录工具
- **device-tree-compiler**: 设备树编译器
- **python3-dev**: Python 开发头文件
- **python3-venv**: Python 虚拟环境
- **gcc/g++-multilib**: 多架构编译器

## 2. 验证工具版本

安装完成后，验证关键工具的版本：

```bash
# 验证 CMake 版本（需要 3.20 或更高）
cmake --version

# 验证 Python 版本（需要 3.8 或更高）
python3 --version

# 验证设备树编译器版本
dtc --version
```

## 3. 配置 Python 环境

### 3.1 创建 Python 虚拟环境


```bash
# 创建虚拟环境目录
mkdir -p ~/zephyrproject

# 创建虚拟环境
python3 -m venv ~/zephyrproject/.venv

# 激活虚拟环境
source ~/zephyrproject/.venv/bin/activate
```

### 3.2 升级 pip 并安装 west

```bash
# 升级 pip 到最新版本
pip install --upgrade pip

# 安装 west 工具
pip install west
```

### 3.3 验证 west 安装

```bash
# 验证 west 版本
west --version
```

注意west版本应为v1.5.0或更高版本。

## 4. 获取 Zephyr 源码

### 4.1 初始化工作空间

使用个人定制的 Zephyr 仓库初始化工作空间：

```bash
# 创建项目目录
cd ~/zephyrproject

# 使用个人仓库初始化 west 工作空间
west init -m https://github.com/GD32-MCU-IOT/gd32_zephyr.git .
```

### 4.2 导出zephyr cmake包

导出 Zephyr CMake 包。这允许 CMake 自动加载构建 Zephyr 应用程序所需的样板代码。

```bash
west zephyr-export
```

### 4.3 更新所有模块

```bash
# 更新所有 west 模块
west update
```

这个过程可能需要几分钟时间，west 会下载 Zephyr 内核和所有必需的模块。

## 5. 安装 Python 依赖

```bash
# 进入 zephyr 目录
cd zephyr

# 安装 Python 依赖
west packages pip --install
```

## 6. 安装 Zephyr SDK

### 6.1 下载并安装 SDK

```bash
# 下载 Zephyr SDK（版本可能会更新，请检查最新版本）
west sdk install
```

## 7. 环境设置

每次开发时，需要设置 Zephyr 环境：

```bash
# 激活 Python 虚拟环境
source ~/zephyrproject/.venv/bin/activate

```

## 8. 验证安装

### 8.1 构建示例项目

```bash
# 进入 zephyr 目录
cd ~/zephyrproject/zephyr

# 构建 Hello World 示例（以 GD32F470I-EVAL 为例）
west build -p always -b gd32f470i_eval samples/hello_world
```

### 8.2 检查构建结果

如果构建成功，您将看到类似以下的输出：

```
[100%] Built target zephyr
```

构建产物将位于 `build/zephyr/` 目录中。

## 9. GD32 特定配置

### 9.1 支持的 GD32 开发板

当前支持的 GD32 开发板包括：
- gd32f450i_eval
- gd32f407v_start
- gd32f403z_eval
- gd32e103v_eval
- （更多板子请查看 boards/arm/ 目录）

### 9.2 查看可用的 GD32 板子

```bash
# 查看所有支持的 GD32 板子
west boards | grep gd32
```

## 10. 常用开发命令

### 10.1 构建项目

```bash
# 构建指定板子的项目
west build -p always -b <board_name> <sample_path>

# 例如：构建 GD32F470I-EVAL 的 hello_world
west build -p always -b gd32f470i_eval samples/hello_world
```

### 10.2 清理构建

```bash
# 清理构建文件
west build -t clean

# 或者删除整个构建目录
rm -rf build
```

## 11. 故障排除

### 11.1 常见问题

1. **Python 版本问题**：确保使用 Python 3.8+
2. **权限问题**：某些操作可能需要 sudo 权限
3. **网络问题**：确保能够访问 GitHub 和相关下载源
4. **SDK 路径问题**：检查 ZEPHYR_SDK_INSTALL_DIR 环境变量是否正确设置

### 11.2 获取帮助

```bash
# 查看 west 帮助
west --help

# 查看特定命令帮助
west build --help
```

## 12. 开发工作流

1. **激活环境**：每次开始开发前激活 Python 虚拟环境和 Zephyr 环境
2. **选择板子**：根据您的硬件选择对应的板子配置
3. **编写代码**：在 `src/` 目录中编写应用程序代码
4. **配置项目**：编辑 `prj.conf` 和 `CMakeLists.txt` 文件
5. **构建测试**：使用 `west build` 构建项目

## 参考资源

- [Zephyr 官方文档](https://docs.zephyrproject.org/)
- [GD32 官方网站](https://www.gigadevice.com/)
- [GD32 Zephyr 仓库](https://github.com/GD32-MCU-IOT/gd32_zephyr.git)

---

**注意**：本指南基于 Zephyr 4.2+ 版本编写，不同版本可能存在细微差异。如遇到问题，请参考官方文档或在相关社区寻求帮助。
