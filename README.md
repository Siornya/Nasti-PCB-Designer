# Nasti PCB Designer

将普通图片处理为沉金图案或保留原色的彩色丝印，再转换成可导入嘉立创EDA专业版的 PCB 图案工程。

## 下载与安装

在 Release 中选择对应系统的文件：

| 平台 | 架构 | 下载产物 | 启动方式 |
| --- | --- | --- | --- |
| Windows | x64 | `nasti-pcb-designer-windows-x64.exe` | 运行安装程序，安装后从开始菜单启动 |
| macOS | Apple Silicon / ARM64 | `nasti-pcb-designer-macos-arm64.dmg` | 打开后将 `.app` 拖入 Applications |
| Linux | x64 | `nasti-pcb-designer-linux-x64.AppImage` | 赋予执行权限后运行，无需解压 |

下载文件已包含运行所需依赖，无需另装 Qt 或 Python。Linux 下载后首次运行可执行 `chmod +x nasti-pcb-designer-linux-x64.AppImage`；没有 FUSE 时可用 `APPIMAGE_EXTRACT_AND_RUN=1 ./nasti-pcb-designer-linux-x64.AppImage`。Linux 需要兼容 Ubuntu 24.04 的系统和桌面图形环境；macOS 为临时签名，没有 Apple 开发者签名或公证。

## 功能

- 主预览支持鼠标位置滚轮缩放、拖动平移、100% 原始像素显示和适应窗口；双击恢复适应窗口。参数更新时保留查看位置。
- 分开管理 PCB 正面和背面的多张图片图层，分别调整位置、显示状态和叠放顺序。
- 每个图层可选择沉金、彩色丝印或描边。描边图层仅保留轮廓，按沉金导出，可与保留原图的丝印图层叠放。
- 提供 8 种阈值方法，包括 Triangle 和 Li 自动阈值；Try All 使用双列大图、显示自动计算的阈值，并可直接调整算法参数后重新比较。
- 调整曝光、对比度、伽马、降噪、锐化和局部增强等参数。
- 沉金和丝印图层都可选择用 Canny 提取并叠加图案边缘；可调节双阈值和描边宽度，默认线宽 2 px。沉金保留原有二值化结果，丝印保留原色和透明度；透明底图片也会提取透明边界。图案内部线条同样可被提取。
- 可选轮廓简化，默认容差 2 px；在保留线条端点、分叉和闭环的前提下减少细小锯齿。无法安全简化时保留原轮廓，状态栏会显示处理结果。
- 根据图片 DPI 自动计算 PCB 物理尺寸；没有可靠 DPI 时按 300 DPI 计算。
- 选择绿、红、黄、蓝、白或雅黑阻焊并导出 `.epro2`，默认使用白色阻焊。
- 自动跟随系统的浅色模式和深色模式。

## 使用桌面应用

1. 在右侧选择“正面”或“背面”，点击“添加图层”选择一张或多张图片。
2. 为沉金图层调整二值化参数，或点击 Try All 比较全部算法。需要描边沉金或丝印时，勾选“提取并叠加边缘（Canny）”，调节阈值和描边宽度；取消勾选则使用原来的图层结果。
   如需仅描边，在右侧图层类型中选择“描边”，再调整边缘双阈值和线宽。切换回“沉金”或“丝印”即可恢复该类型的处理结果。
   需要减少轮廓细节时，可勾选“轮廓简化（Douglas–Peucker）”，调整“简化容差（px）”，默认容差为 2 px；取消勾选或设为 0 即使用未简化的边缘。处理完成后可在状态栏查看是否实际应用，以及预计导出矩形数量的变化。
3. 设置各图层的位置、类型、显示状态和叠放顺序。
4. 选择阻焊颜色和输出目录。
5. 保存黑白图片，或生成包含所有可见图层的 PCB 文件。

导出的 `.epro2` 文件可在嘉立创EDA专业版中打开。正面图案写入顶层，背面图案写入底层；沉金图案生成铜层和阻焊开窗，彩色丝印写入对应面的丝印层。板边四周固定增加 1 mm。

生产前请在嘉立创EDA中检查铜层、阻焊层、DRC、2D/3D 和 Gerber 预览，并确认最小线宽、间距和阻焊偏位符合板厂要求。

## img2pcb 导出器

导出器可以单独运行，相关文档提供在 [Gist](https://gist.github.com/Siornya/2ec228784c060a1ce5e84344bb4d70f9)

## 项目结构

| 目录或文件 | 用途 |
| --- | --- |
| `core/` | 图像预处理、二值化和边缘提取 |
| `app/` | 应用逻辑、图层管理和导出调用 |
| `qml/` | 桌面界面 |
| `img2pcb.py` | PCB 工程导出器 |
| `tests/` | 图像处理、应用和打包测试 |

## 从源码构建

准备 CMake 3.21 以上、C++20 编译器、Qt 6.5 以上和 OpenCV 4。Qt 需包含 Quick、QuickControls2、Concurrent 模块，OpenCV 需包含 core、imgproc、photo 模块。

```sh
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
```

源码运行时，PCB 导出还需要 Python 及 `requirements.txt` 中的依赖。仅构建和测试图像处理核心时，可将上述预设替换为 `core`，无需安装 Qt 或 Python。

如果 CMake 找不到依赖，按安装位置配置：

```sh
cmake --preset release \
	-DOpenCV_DIR="/path/to/opencv4" \
	-DCMAKE_PREFIX_PATH="/path/to/Qt/6.x"
```

构建后运行 `build/release/bin/nasti-pcb-designer`；Windows 多配置构建的程序位于 `build/release/bin/Release/`，构建命令需在 Visual Studio 开发者命令提示符中执行。

## Python 导出环境

创建源码运行所需的导出环境：

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
```

源码运行时，程序会优先使用项目中的 `.venv`。也可以通过 `IMAGE_BINARIZER_PYTHON` 指定其他 Python 解释器。
