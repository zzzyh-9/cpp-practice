# 第 2 周 · JSON 内存优化

使用分配计数器比较 [Simpleson](https://github.com/gregjesl/simpleson) 的原始版本与优化版本。将序列化中的连续字符串拼接改为直接追加，并按估算长度预留结果缓冲区。原始版与优化版均保留上游 MIT 许可和署名。

对同一 50 字段对象进行紧凑序列化，历史 Release 实验结果如下。

| 阶段 | 分配次数 | 累计申请字节数 |
| --- | ---: | ---: |
| 优化前 | 107 | 9,814 |
| 改为直接追加 | 58 | 9,208 |
| 追加前预留容量 | 51 | 3,632 |

计数覆盖测量区间内由计数器记录的堆申请。累计申请字节数与峰值内存、运行时间是不同指标。结果取决于编译器和标准库。

需要 CMake 3.16+、支持 C++17 的编译器，以及下例使用的 Ninja。

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows 下可运行 `build/memory_test_baseline.exe` 和 `build/memory_test_optimized.exe` 查看分配统计。两套序列化测试分别链接原始库与优化库，检查输出一致性。
