// bench_import.cpp
// 测量 ImportPipeline 同步导入的吞吐量，并把导入结果写入 txt 报告。
//
// 用法：
//   csvp_bench <csv_file> [repeat_count] [report_path]
//
// 大文件压测（如 sample_100k.csv）后，生成 txt 报告便于直观核对
// rows_imported / rows_skipped / 错误行明细，避免用 cout 刷屏。
//
// 默认报告文件：bench_report.txt（可用第 3 个参数覆盖）

#include "csvp/pipeline.h"
#include "csvp/parser.h"
#include "csvp/validator.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstdlib>

// Part C：峰值内存测量（Windows）
#include <windows.h>
#include <psapi.h>

using namespace csvp;

// 后台内存采样：每 20 ms 读取进程提交内存（PagefileUsage）与工作集峰值
struct MemorySampler {
    std::atomic<bool> running = true;
    std::atomic<size_t> peak_pagefile_kb = 0;      // 提交内存峰值（私有 commit，稳定可复现）
    std::atomic<size_t> peak_working_set_kb = 0;   // 物理内存工作集峰值
    std::thread thread;

    void start() {
        running = true;
        thread = std::thread([this]() {
            PROCESS_MEMORY_COUNTERS_EX pmc{};
            pmc.cb = sizeof(pmc);
            while (running) {
                if (GetProcessMemoryInfo(GetCurrentProcess(),
                                         reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                                         sizeof(pmc))) {
                    size_t pf = static_cast<size_t>(pmc.PagefileUsage / 1024);
                    size_t ws = static_cast<size_t>(pmc.WorkingSetSize / 1024);
                    if (pf > peak_pagefile_kb.load()) peak_pagefile_kb.store(pf);
                    if (ws > peak_working_set_kb.load()) peak_working_set_kb.store(ws);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });
    }

    void stop() {
        running = false;
        if (thread.joinable()) thread.join();
    }
};

// 单次运行结果快照
struct RunSnapshot {
    size_t rows_imported = 0;
    size_t rows_skipped = 0;
    size_t error_count = 0;
    bool error_limit_hit = false;
    double ms = 0.0;
    std::vector<RowError> errors; // 仅保留少量样本，避免内存膨胀
};

static const char* errorCodeName(ErrorCode c) {
    switch (c) {
        case ErrorCode::Ok: return "Ok";
        case ErrorCode::ParseError: return "ParseError";
        case ErrorCode::ColumnCountMismatch: return "ColumnCountMismatch";
        case ErrorCode::TypeConversionError: return "TypeConversionError";
        case ErrorCode::RequiredFieldMissing: return "RequiredFieldMissing";
        case ErrorCode::RangeError: return "RangeError";
        case ErrorCode::RegexMismatch: return "RegexMismatch";
        case ErrorCode::EncodingError: return "EncodingError";
        case ErrorCode::FileNotOpen: return "FileNotOpen";
        case ErrorCode::InternalError: return "InternalError";
        default: return "Unknown";
    }
}

std::vector<FieldRule> makeDefaultRules() {
    return {
        {"id", FieldType::Int, true},
        {"name", FieldType::String, true},
        {"age", FieldType::Int, true},
        {"score", FieldType::Double, true},
        {"active", FieldType::Bool, false},
    };
}

// 把压测结果写入 txt 报告
void writeReport(const std::string& report_path,
                 const std::string& filepath,
                 int repeat,
                 const std::vector<RunSnapshot>& runs,
                 double avg_ms,
                 size_t rows_per_sec,
                 size_t peak_pagefile_kb,
                 size_t peak_working_set_kb) {
    std::ofstream ofs(report_path, std::ios::out | std::ios::trunc);
    if (!ofs) {
        std::cerr << "Cannot open report file: " << report_path << "\n";
        return;
    }

    ofs << "CSV Import Pipeline Benchmark Report\n";
    ofs << "====================================\n\n";
    ofs << "File            : " << filepath << "\n";
    ofs << "Repeats         : " << repeat << "\n";
    ofs << "Avg time        : " << avg_ms << " ms\n";
    ofs << "Throughput      : " << rows_per_sec << " rows/sec\n";
    ofs << "Peak memory     : " << peak_pagefile_kb << " KB (commit) / "
        << peak_working_set_kb << " KB (working set)\n\n";

    ofs << "--- Per-run Summary ---\n";
    ofs << "run  rows_imported  rows_skipped  errors  error_limit_hit  time_ms\n";
    for (size_t i = 0; i < runs.size(); ++i) {
        const RunSnapshot& s = runs[i];
        ofs << (i + 1) << "    "
            << s.rows_imported << "              "
            << s.rows_skipped << "            "
            << s.error_count << "       "
            << (s.error_limit_hit ? "yes" : "no") << "              "
            << s.ms << "\n";
    }

    // 错误行明细（只写最后一次运行的样本，最多列出实际收集到的）
    ofs << "\n--- Error Rows (sample) ---\n";
    if (runs.empty() || runs.back().errors.empty()) {
        ofs << "(no error rows collected in this run)\n";
    } else {
        const std::vector<RowError>& errs = runs.back().errors;
        for (size_t i = 0; i < errs.size(); ++i) {
            const RowError& e = errs[i];
            ofs << "line " << e.line_number << ": "
                << e.raw_line << "\n";
            for (const CellError& ce : e.cell_errors) {
                ofs << "    [col " << ce.column_index << " '"
                    << ce.column_name << "'] value='" << ce.raw_value
                    << "' code=" << errorCodeName(ce.code)
                    << " msg=" << ce.message << "\n";
            }
        }
    }

    ofs << "\n--- Note ---\n";
    ofs << "This report is generated by csvp_bench for visual inspection.\n";
    ofs << "Use rows_imported / rows_skipped / error rows to verify correctness.\n";
}

void runBench(const char* filepath, int repeat, const std::string& report_path,
              const std::string& error_file) {
    ImportPipeline::Config cfg;
    cfg.parser.delimiter = ',';
    cfg.parser.has_header = true;
    cfg.skip_bad_rows = true;
    cfg.error_file = error_file;  // Part C：错误详情落盘（空 = 内存收集）

    double total_ms = 0;
    size_t total_rows = 0;
    std::vector<RunSnapshot> runs;
    runs.reserve(static_cast<size_t>(repeat));

    const size_t kMaxErrorSamples = 200; // 限制报告中的错误行样本数

    // Part C：内存采样（整个压测期间记录峰值）
    MemorySampler mem;
    mem.start();

    for (int r = 0; r < repeat; ++r) {
        ImportPipeline pipeline(cfg, makeDefaultRules());

        auto t0 = std::chrono::steady_clock::now();
        auto result = pipeline.importSync(filepath);
        auto t1 = std::chrono::steady_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        total_ms += ms;
        total_rows += result.rows_imported;

        RunSnapshot snap;
        snap.rows_imported = result.rows_imported;
        snap.rows_skipped = result.rows_skipped;
        snap.error_count = result.errors.size();
        snap.error_limit_hit = (cfg.error_limit > 0 && result.errors.size() >= cfg.error_limit);
        snap.ms = ms;
        // 只收集少量样本，避免 100k 脏数据全量拷贝占内存
        for (size_t i = 0; i < result.errors.size() && i < kMaxErrorSamples; ++i) {
            snap.errors.push_back(result.errors[i]);
        }
        runs.push_back(snap);

        std::cout << "  Run " << (r + 1) << ": "
                  << result.rows_imported << " rows in "
                  << ms << " ms";
        if (result.rows_skipped > 0) {
            std::cout << " (" << result.rows_skipped << " skipped)";
        }
        if (!result.errors.empty()) {
            std::cout << " [" << result.errors.size() << " error rows]";
        }
        std::cout << std::endl;
    }

    mem.stop();
    size_t peak_pf = mem.peak_pagefile_kb.load();
    size_t peak_ws = mem.peak_working_set_kb.load();

    double avg_ms = total_ms / repeat;
    size_t rows_per_sec = static_cast<size_t>((total_rows / repeat) / (avg_ms / 1000.0));

    std::cout << "\n--- Summary ---\n";
    std::cout << "File: " << filepath << "\n";
    std::cout << "Repeats: " << repeat << "\n";
    std::cout << "Avg rows: " << (total_rows / repeat) << "\n";
    std::cout << "Avg time: " << avg_ms << " ms\n";
    std::cout << "Throughput: " << rows_per_sec << " rows/sec\n";
    std::cout << "Peak memory: " << peak_pf << " KB (commit) / "
              << peak_ws << " KB (working set)\n";
    if (!error_file.empty()) {
        std::cout << "Error detail file: " << error_file << "\n";
    }

    // 生成 txt 报告
    writeReport(report_path, filepath, repeat, runs, avg_ms, rows_per_sec,
                peak_pf, peak_ws);
    std::cout << "\nReport written to: " << report_path << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: csvp_bench <csv_file> [repeat_count] [report_path] [error_file]\n";
        std::cerr << "  csv_file    : path to CSV file\n";
        std::cerr << "  repeat_count: number of benchmark repeats (default: 3)\n";
        std::cerr << "  report_path : output txt report (default: bench_report.txt)\n";
        std::cerr << "  error_file  : optional path for error detail dump (Part C)\n";
        return 1;
    }

    const char* filepath = argv[1];
    int repeat = (argc >= 3) ? std::atoi(argv[2]) : 3;
    if (repeat < 1) repeat = 1;
    std::string report_path = (argc >= 4) ? argv[3] : "bench_report.txt";
    std::string error_file = (argc >= 5) ? argv[4] : "";

    std::cout << "CSV Import Pipeline Benchmark\n";
    std::cout << "=============================\n\n";

    try {
        runBench(filepath, repeat, report_path, error_file);
    } catch (const std::exception& e) {
        std::cerr << "Benchmark failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
