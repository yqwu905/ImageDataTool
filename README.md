# 图片对比 / ImageCompare

[![Build and Release](https://github.com/yqwu905/ImageDataTool/actions/workflows/build-release.yml/badge.svg)](https://github.com/yqwu905/ImageDataTool/actions/workflows/build-release.yml)

Windows 10（1703+）/ Windows 11 x64 图片筛选与同步处理工具。C++17 + Win32 + Windows 自带 WIC/GDI+，运行库静态链接，单 EXE 免安装。

- 添加多个文件夹，按自然顺序或完整文件名匹配并排对比，缺图保留空位。
- 为当前整组图片同步添加黑色矩形，支持撤销、重做和非破坏性 PNG 导出。
- 支持单图轮看、左键拖动同步平移、以鼠标为中心缩放，A/B/C/D 标记、自定义类别及筛选。
- 保存项目，批量导出图片及标记 CSV。

从 [Releases](https://github.com/yqwu905/ImageDataTool/releases) 下载 `ImageCompare.exe`，或解压 `ImageCompare-v版本号-windows-x64.zip`。完整使用方式见 [README.txt](README.txt)。开发版构建产物在 [Actions](https://github.com/yqwu905/ImageDataTool/actions/workflows/build-release.yml) 对应运行的 Artifacts 中保存 14 天。

## 本地构建

安装 Visual Studio 2022 Build Tools 的“使用 C++ 的桌面开发”及 CMake，在 PowerShell 执行：

```powershell
./build-windows.ps1
```

默认使用 `MinSizeRel`、x64 和静态运行库，先编译并运行核心及 Windows/GDI+ 集成测试，再输出 `dist/ImageCompare.exe`。需要打包时：

```powershell
./build-windows.ps1 -Configuration MinSizeRel -Package
```

也可使用 MinGW-w64 执行 `build-mingw.cmd`。Linux 上可运行与界面无关的核心测试：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

## 自动构建与发布

分支 push、PR 和手动触发运行 Linux 核心测试、Windows x64 编译及原生 GDI+/Win32 集成测试。测试失败时不产生可发布包；日志保留为 Artifact。

推送合法 `vMAJOR.MINOR.PATCH` 标签后，两组测试通过才自动创建 Release。`v1.1.0-rc.1` 等带后缀的标签发布为预发布版本，不替换最新稳定版。

发布步骤：

1. 修改 `CMakeLists.txt` 的 `project(... VERSION ...)` 和 `assets/app.rc` 中的数字、字符串版本信息，保持一致。
2. 提交到 `master`，等待分支构建通过。
3. 创建并推送版本标签：

```sh
git tag -a v1.1.0 -m "ImageCompare 1.1.0"
git push origin v1.1.0
```

后续版本请使用新版本号。流水线会校验标签与 CMake/Windows 资源的数字版本一致，再打包发布：

- `ImageCompare.exe`：单文件便携程序。
- `ImageCompare-v版本号-windows-x64.zip`：程序、中文说明、验证说明及构建信息。
- `ImageCompare-v版本号-source.zip`：对应提交的源码归档。
- `BUILD-INFO.txt`：版本、提交 SHA、构建时间、Actions 运行链接。
- `SHA256SUMS.txt`：上述附件的 SHA256 校验值。

发布时先创建草稿，全部附件上传成功后再公开。重跑已有草稿可继续上传；已公开的 Release 会保留原附件，避免重新构建改变已发布文件。分支或手动运行只构建，不创建版本标签或 Release。

仅发布作业具有 `contents: write` 权限，使用 GitHub 自动生成的 `GITHUB_TOKEN`，无需额外配置个人 Token。详情见 [.github/workflows/build-release.yml](.github/workflows/build-release.yml)。

## 验证范围

[VALIDATION.txt](VALIDATION.txt) 记录初始本地交叉构建及 Wine 验证；每个发布版本的 Windows 原生自动测试结果以 `BUILD-INFO.txt` 链接的 Actions 运行记录为准。文件选择器、高 DPI 多屏及相机文件兼容性仍需人工验收。

## 性能复现

扫描、排序和配对在后台运行；预览优先加载当前组并预读前后组，快速切图会丢弃过时的等待任务。绘制复用缓冲区、字体和已解码的位图，只处理可见区域。拖动和滚轮连续操作时使用快速缩放，停止后恢复精细显示。导出仍使用原始尺寸和像素。

Windows Actions 会在同一台运行器上运行当前版与 `v1.0.0` 的对比基准：3 个文件夹、每夹 1,000 个文件、4096×3072 JPEG。报告分别记录添加操作返回时间、首组就绪时间、适应/4倍/12倍缩放绘制中位数，以及切换下一组就绪时间。绘制基准模拟拖动时的快速显示模式；静止后的精细显示由交互回归检查覆盖。JSON 报告随 `test-results-windows` Artifact 保存；它们是受运行器负载影响的实测数据，不作为固定帧率承诺。基准使用同一张样图的硬链接来控制图像内容差异，不能代表所有磁盘、相机编码或网络目录。

本地复现：配置 CMake 时增加 `-DIMAGECOMPARE_BUILD_BENCHMARKS=ON`，编译后运行：

```powershell
./build/MinSizeRel/performance_benchmark.exe build/perf-fixtures build/performance-after.json
```

可再设置 `-DIMAGECOMPARE_BASELINE_DIR=旧版本源码目录` 构建 `performance_baseline`，用同一测试目录运行优化前版本。
