# GD32 MCU Zephyr Getting Started Guide

This document provides a comprehensive guide for setting up and configuring the development environment for GD32 MCU development using a customized Zephyr repository.

## Prerequisites

- Linux operating system (Ubuntu 22.04 or higher recommended)
- Active internet connection
- sudo privileges

## 1. Install System Dependencies

First, install the required system dependencies for Zephyr development:

```bash
sudo apt install --no-install-recommends git cmake ninja-build gperf \
  ccache dfu-util device-tree-compiler wget python3-dev python3-venv python3-tk \
  xz-utils file make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1
```

### Package Description:
- **git**: Version control system
- **cmake**: Build system generator
- **ninja-build**: Fast build tool
- **gperf**: Perfect hash function generator
- **ccache**: Compiler cache
- **dfu-util**: DFU firmware flashing utility
- **device-tree-compiler**: Device tree compiler
- **python3-dev**: Python development headers
- **python3-venv**: Python virtual environment
- **gcc/g++-multilib**: Multi-architecture compilers

## 2. Verify Tool Versions

After installation, verify the versions of key tools:

```bash
# Verify CMake version (requires 3.20 or higher)
cmake --version

# Verify Python version (requires 3.8 or higher)
python3 --version

# Verify device tree compiler version
dtc --version
```


## 3. Configure Python Environment

### 3.1 Create Python Virtual Environment

```bash
# Create virtual environment directory
mkdir -p ~/zephyrproject

# Create virtual environment
python3 -m venv ~/zephyrproject/.venv

# Activate virtual environment
source ~/zephyrproject/.venv/bin/activate
```

### 3.2 Upgrade pip and Install west

```bash
# Upgrade pip to latest version
pip install --upgrade pip

# Install west tool
pip install west
```

### 3.3 Verify west Installation

```bash
# Verify west version
west --version
```

The version of west should be v1.5.0 or higher.


## 4. Get Zephyr Source Code

### 4.1 Initialize Workspace

Initialize the workspace using the customized Zephyr repository:

```bash
# Create project directory
cd ~/zephyrproject

# Initialize west workspace with personal repository
west init -m https://github.com/GD32-MCU-IOT/gd32_zephyr.git .
```

### 4.2 Export Zephyr CMake Package

Export a Zephyr CMake package. This allows CMake to automatically load boilerplate code required for building Zephyr applications.

```bash
west zephyr-export
```

### 4.3 Update All Modules

```bash
# Update all west modules
west update
```

This process may take several minutes as west downloads the Zephyr kernel and all required modules.

## 5. Install Python Dependencies

```bash
# Enter zephyr directory
cd zephyr

# Install Python dependencies
west packages pip --install
```

## 6. Install Zephyr SDK

### 6.1 Download and Install SDK

```bash
# Download Zephyr SDK (version may be updated, please check for latest version)
west sdk install
```

## 7. Environment Setup

Each time you start development, you need to set up the Zephyr environment:

```bash
# Activate Python virtual environment
source ~/zephyrproject/.venv/bin/activate

```

## 8. Verify Installation

### 8.1 Build Sample Project

```bash
# Enter zephyr directory
cd ~/zephyrproject/zephyr

# Build Hello World sample (using GD32F470I-EVAL as example)
west build -p always -b gd32f470i_eval samples/hello_world
```

### 8.2 Check Build Results

```bash
# Check build results
If the build is successful, you will see output similar to:

```
[100%] Built target zephyr
```

Build artifacts will be located in the `build/zephyr/` directory.

## 9. GD32 Specific Configuration

### 9.1 Supported GD32 Development Boards

Currently supported GD32 development boards include:
- gd32f450i_eval
- gd32f407v_start
- gd32f403z_eval
- gd32e103v_eval
- (For more boards, check the boards/arm/ directory)

### 9.2 View Available GD32 Boards

```bash
# View all supported GD32 boards
west boards | grep gd32
```

## 10. Common Development Commands

### 10.1 Build Project

```bash
# Build project for specified board
west build -p always -b <board_name> <sample_path>

# Example: Build hello_world for GD32F470I-EVAL
west build -p always -b gd32f470i_eval samples/hello_world
```

### 10.2 Clean Build

```bash
# Clean build files
west build -t clean

# Or delete entire build directory
rm -rf build
```

## 11. Troubleshooting

### 11.1 Common Issues

1. **Python version issues**: Ensure you're using Python 3.8+
2. **Permission issues**: Some operations may require sudo privileges
3. **Network issues**: Ensure access to GitHub and related download sources
4. **SDK path issues**: Check if ZEPHYR_SDK_INSTALL_DIR environment variable is set correctly

### 11.2 Getting Help

```bash
# View west help
west --help

# View specific command help
west build --help
```

## 12. Development Workflow

1. **Activate environment**: Activate Python virtual environment and Zephyr environment before starting development
2. **Select board**: Choose the corresponding board configuration based on your hardware
3. **Write code**: Write application code in the `src/` directory
4. **Configure project**: Edit `prj.conf` and `CMakeLists.txt` files
5. **Build and test**: Use `west build` to build the project

## Reference Resources

- [Zephyr Official Documentation](https://docs.zephyrproject.org/)
- [GD32 Official Website](https://www.gigadevice.com/)
- [GD32 Zephyr Repository](https://github.com/GD32-MCU-IOT/gd32_zephyr.git)

---

**Note**: This guide is written based on Zephyr 4.2+ version. Different versions may have minor differences. If you encounter issues, please refer to the official documentation or seek help in relevant communities.
