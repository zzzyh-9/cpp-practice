# 第 1 周 · CSV 导入管道

用 C++17 实现 CSV 解析、校验与导入。解析器处理 UTF-8、GBK、引号转义和换行边界；校验器按列执行必填、类型、范围和正则检查；导入管道支持同步导入、后台流式导入、进度回调和取消。

此项目使用 Windows API。需要 Windows、CMake 3.16+、支持 C++17 的编译器和 Ninja。测试优先使用已安装的 GoogleTest，否则构建时获取 v1.15.2。数据目录只包含测试样本，10 万行样本含 2,000 行故意设置的无效数据。

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

压测示例（Windows PowerShell）

```powershell
.\build\csvp_bench.exe data/sample_100k.csv 3 build/benchmark.txt
```

实现参考 [fast-cpp-csv-parser](https://github.com/ben-strasser/fast-cpp-csv-parser) 的设计思路。
