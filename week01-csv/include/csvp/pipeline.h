#pragma once

#include "types.h"
#include "parser.h"
#include "validator.h"

#include <future>
#include <functional>
#include <chrono>
#include <atomic>
#include <thread>
#include <memory>
#include <fstream>
#include <mutex>

namespace csvp {

// alignas(64) 热计数器（伪共享修复，见类内 HotCounter）会使 ImportPipeline
// 因对齐说明符产生结构填充。MSVC C4324 对任何 64 字节对齐需求都会触发，
// 属预期而非缺陷：显式抑制并注释原因（Part C C-1 编译告警清理）。
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // 结构因对齐说明符被填充（有意为之）
#endif

// ImportPipeline: CSV 导入管道的顶层入口。
// 串联 CsvParser（解析）+ FieldValidator（校验）+ 错误收集。
//
// 两种导入模式：
// - importSync:  同步导入，阻塞直到全部完成
// - importStreaming: 流式导入，支持进度回调和提前终止（Part B）
//
class ImportPipeline {
public:
    struct Config {
        CsvParser::Config parser;
        bool skip_bad_rows = false;   // 是否跳过坏行继续导入
        size_t error_limit = 0;       // 最大收集错误数；0 表示不限制（全部收集）
        // Part C：错误详情落盘路径。空 = 错误详情收集进 Result.errors（内存驻留）；
        // 非空 = 每条错误追加写入该文件（行号|原因|原始内容），内存只保留错误计数，
        // 满足 1GB 文件 50MB 峰值内存约束下"错误可事后排查"的要求。
        std::string error_file;
    };

    struct Result {
        size_t rows_imported = 0;
        size_t rows_skipped = 0;
        std::vector<RowError> errors; // 错误详情列表
        double elapsed_ms = 0.0;
    };

    ImportPipeline() = default;

    // 传入解析器配置和校验规则列表
    ImportPipeline(const Config& cfg, std::vector<FieldRule> rules);

    // ---- 同步导入（当前实现）----
    Result importSync(const char* filepath);

    // ---- 流式导入 ----
    // 和 importSync 做同一件事，但放到后台线程里做：调用后马上返回，
    // 不占住调用方的线程。
    //   1. 读文件、解析、校验都在后台线程上完成，当前线程不等
    //   2. 每处理完 1000 行，调用一次 progress(已处理的行数) 回调
    //   3. progress 返回 false 时，当作调用方要求停止，尽快收尾
    //   4. 最终结果（被叫停时就是已完成的那部分）通过返回的 std::future 交出
    //   5. 后台线程要经常看一眼停止标志，否则 shutdown() 喊停后
    //      它可能还在埋头干活，停不及时
    std::future<Result> importStreaming(const char* filepath,
                                         std::function<bool(size_t processed)> progress);

    // 请求停止正在进行的流式导入，最多等 timeout 毫秒。
    //   1. 先置位停止标志，通知后台线程收尾
    //   2. 最多等 timeout 毫秒，到点就返回，不强行结束线程
    //   3. 返回时把未完成的任务数写进 pending_count_
    //   4. 不能死锁、泄漏资源或崩溃
    //   5. 不要用"一把大锁锁住全部操作"的方式实现
    void shutdown(std::chrono::milliseconds timeout);

    // shutdown(timeout) 返回后，读取"尚未完成的任务数"（行数）。
    // 后台线程已收尾时为 0；超时未收尾时为估算值（总行数 - 已处理行数）。
    size_t pendingCount() const {
        return pending_count_.load(std::memory_order_relaxed);
    }

    // ---- 统计信息 ----
    Stats stats() const;

    ~ImportPipeline();

private:
    bool validateAndCollect(Row& row, size_t line_number,
                            const std::string& raw_line, Result& result);

    // Part C：错误详情分流（落盘 / 内存收集）
    void collectError(RowError& err, Result& result);

    // 后台线程主体：打开文件 -> 逐行解析/校验 -> 进度回调 -> 收尾交付。
    void runStreaming(const std::string& filepath,
                      std::function<bool(size_t)> progress,
                      std::shared_ptr<std::promise<Result>> promise);

    // 预扫描文件总行数（二进制逐块数 '\n'），供 shutdown 后估算未完成任务。
    static size_t countLines(const std::string& filepath);

    Config cfg_;
    std::vector<FieldRule> rules_;
    FieldValidator validator_;

    // Part C：错误详情落盘句柄。Config::error_file 非空时打开；
    // 错误行在 validateAndCollect 内直接写入，收尾时关闭。
    std::ofstream error_stream_;

    // ---- 热路径统计计数器 ----
    // 供 importStreaming 的后台线程在每行处理时递增。
    // 每个计数器独立 struct 并 alignas(64)（Part A #11 防伪共享）：
    // 对齐落在类型定义上而非类成员说明符上，不触发 MSVC C4324
    // "由于对齐说明符，结构被填充" 告警（Part C C-1）。
    struct alignas(64) HotCounter {
        std::atomic<size_t> v = 0;
    };
    HotCounter hot_total_lines_;
    HotCounter hot_rows_imported_;
    HotCounter hot_rows_skipped_;

    // ---- 停止标志 ----
    std::atomic<bool> stop_requested_ = false;
    std::atomic<size_t> pending_count_{0};  // shutdown 超时后的未完成任务数

    // ---- Part B 后台线程运行时状态 ----
    std::thread worker_;                  // 流式导入的后台线程
    std::atomic<bool> streaming_active_ = false;  // 是否有活跃的流式导入
    std::atomic<size_t> total_known_ = 0;         // 总行数是否已由预扫描得出（0=未知）
    std::atomic<size_t> total_lines_est_ = 0;     // 预扫描得到的总行数（含表头，估算用）

    Stats stats_;
    mutable std::mutex stats_mutex_;
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace csvp
