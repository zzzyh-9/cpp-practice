# 第 3 周 · Qt Widgets 课堂练习

- `01-login`：输入框、密码隐藏、按钮与消息框。固定账号密码是演示数据。
- `02-hello-labels`：点击按钮，通过信号与槽更新两个标签。
- `04-rescue-apple`：**课堂原型，仍在开发中。** 将游戏模型、控制器和视图拆分，已实现字母下落、键盘匹配、暂停和关卡推进。

需要 CMake 3.16+、C++17 和 Qt 5.15 的 Widgets、Multimedia、Test 模块。将匹配的 Qt 工具链加入 PATH，并在需要时向 CMake 传入 `-DCMAKE_PREFIX_PATH=<Qt 安装目录>`。

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

在 Windows PowerShell 中启动原型。

```powershell
.\build\04-rescue-apple\week03_rescue_apple.exe
```

点击「开始游戏」后输入苹果上的字母。空格暂停或继续，F11 切换全屏。公开版的苹果和背景由 Qt 绘制，音乐与击中音效由简单波形合成。界面和玩法还会继续完善。

右侧分别显示本关救下进度和本局漏接数。暂停时苹果停在原位并显示问号。设置面板分为「玩法」和「音频」，可以分别调整难度、同屏数量、过关目标、漏接上限和音量。

如果只想构建「拯救苹果」，进入 `04-rescue-apple` 后执行上面的 CMake 命令。单独构建时，游戏程序位于 `build/week03_rescue_apple.exe`。
