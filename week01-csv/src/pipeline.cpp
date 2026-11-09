#include "csvp/pipeline.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <fstream>
#include <memory>

namespace csvp {

namespace {
const char* errorCodeName(ErrorCode c) {
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
} // anonymous namespace

ImportPipeline::ImportPipeline(const Config& cfg, std::vector<FieldRule> rules)
    : cfg_(cfg), rules_(std::move(rules)) {
    validator_.addRules(rules_);
}

// ============================================================
// importSync
// ============================================================

ImportPipeline::Result ImportPipeline::importSync(const char* filepath) {
    Result result;
    auto t0 = std::chrono::steady_clock::now();

    // Part C：错误落盘（Config::error_file 非空时打开；失败则降级为内存收集）
    if (!cfg_.error_file.empty()) {
        error_stream_.open(cfg_.error_file, std::ios::out | std::ios::trunc);
    }

    CsvParser parser(cfg_.parser);
    if (!parser.parse(filepath)) {
        // 文件打开或表头解析失败
        RowError fatal;
        fatal.line_number = 0;
        fatal.raw_line = parser.lastError();
        result.errors.push_back(std::move(fatal));
        if (error_stream_.is_open()) error_stream_.close();
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_ = Stats{};
    }

    Row row;
    size_t processed_lines = 0;
    while (parser.readRow(row)) {
        ++processed_lines;

        try {
            // ---- 列数检查 ----
            if (row.size() != parser.header().size()) {
                std::ostringstream oss;
                oss << "Column count mismatch at line " << parser.lineNumber()
                    << ": expected " << parser.header().size()
                    << " columns, got " << row.size();
                throw std::runtime_error(oss.str());
            }

            // ---- 校验与收集 ----
            const bool valid = validateAndCollect(
                row, parser.lineNumber(), parser.lastRawLine(), result);
            if (!valid && !cfg_.skip_bad_rows) {
                throw std::runtime_error(
                    "Validation failed and skip_bad_rows is disabled");
            }
        } catch (const std::runtime_error& e) {
            // 非跳过模式：立即失败（保持原行为，异常穿透到调用方）
            if (!cfg_.skip_bad_rows) {
                throw;
            }
            // 跳过模式：列数不匹配等整行级错误记录后继续，不中断导入
            ++result.rows_skipped;
            if (cfg_.error_limit == 0 || result.errors.size() < cfg_.error_limit) {
                RowError row_err;
                row_err.line_number = parser.lineNumber();

                row_err.raw_line = parser.lastRawLine();

                CellError ce;
                ce.column_index = 0;
                ce.code = ErrorCode::ColumnCountMismatch;
                ce.message = e.what();
                row_err.cell_errors.push_back(std::move(ce));
                collectError(row_err, result);
            }
        }
    }

    // 后处理
    auto t1 = std::chrono::steady_clock::now();
    result.elapsed_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_lines = processed_lines;
        stats_.bytes_read = parser.bytesRead();
        stats_.rows_imported = result.rows_imported;
        stats_.rows_skipped = result.rows_skipped;
        stats_.elapsed_ms = result.elapsed_ms;
    }

    if (error_stream_.is_open()) error_stream_.close();
    return result;
}

// ============================================================
// collectError：错误详情分流（Part C）
// 落盘模式（error_stream_ 已打开）：追加写"行号|原因|原始内容"，内存不驻留明细；
// 内存模式：按原语义 push 进 Result.errors。
// ============================================================

void ImportPipeline::collectError(RowError& err, Result& result) {
    if (error_stream_.is_open()) {
        // 落盘：逐条可事后排查（行号 + 原因 + 原始内容）
        error_stream_ << "line=" << err.line_number << " raw=" << err.raw_line << "\n";
        for (const CellError& ce : err.cell_errors) {
            error_stream_ << "  code=" << errorCodeName(ce.code)
                          << " col=" << ce.column_index
                          << " name=" << ce.column_name
                          << " value=" << ce.raw_value
                          << " msg=" << ce.message << "\n";
        }
        return;  // 不驻留内存
    }
    result.errors.push_back(std::move(err));
}

// ============================================================
// validateAndCollect
// ============================================================

bool ImportPipeline::validateAndCollect(Row& row, size_t line_number,
                                        const std::string& raw_line,
                                        Result& result) {
    auto cell_errors = validator_.validate(row);
    if (cell_errors.empty()) {
        ++result.rows_imported;
        return true;
    }

    // #6：error_limit 已满时只累计跳过行数，不再构造错误明细。
    if (cfg_.error_limit == 0 || result.errors.size() < cfg_.error_limit) {
        RowError row_err;
        row_err.line_number = line_number;
        row_err.raw_line = raw_line;
        row_err.cell_errors = std::move(cell_errors);
        collectError(row_err, result);
    }

    if (cfg_.skip_bad_rows) {
        ++result.rows_skipped;
        return false;
    }

    return false;
}

// ============================================================
// importStreaming
//
// 现在的 importSync 是"一口气读完"：调用方一旦调用它，当前线程就被占住，
// 直到整个文件处理完才返回。这期间既看不到进度，也没办法喊停，
// 文件一大就要干等很久。
//
// importStreaming 做的是同一件事，只是换了干活的方式：
// 调用后马上返回一个 future，真正的读文件、解析、校验交给后台线程；
// 干活过程中定期汇报进度，调用方不满意可以随时叫停。
// ============================================================

std::future<ImportPipeline::Result> ImportPipeline::importStreaming(
    const char* filepath,
    std::function<bool(size_t)> progress) {
    // 若上一次导入的后台线程仍未结束（例如调用方提前放弃了 future），
    // 先请求其收尾并等待，保证同一实例串行复用、不残留线程。
    if (worker_.joinable()) {
        stop_requested_.store(true, std::memory_order_relaxed);
        worker_.join();
    }

    // 重置本轮状态
    stop_requested_.store(false, std::memory_order_relaxed);
    pending_count_.store(0, std::memory_order_relaxed);
    total_known_.store(0, std::memory_order_relaxed);
    total_lines_est_.store(0, std::memory_order_relaxed);
    hot_total_lines_.v.store(0, std::memory_order_relaxed);
    hot_rows_imported_.v.store(0, std::memory_order_relaxed);
    hot_rows_skipped_.v.store(0, std::memory_order_relaxed);
    streaming_active_.store(true, std::memory_order_relaxed);

    // Part C：错误落盘（Config::error_file 非空时打开；失败则降级为内存收集）。
    // 句柄在导入线程内写入、收尾时关闭，调用方在 future 就绪后可立即读取文件。
    if (!cfg_.error_file.empty()) {
        error_stream_.open(cfg_.error_file, std::ios::out | std::ios::trunc);
    }

    auto promise = std::make_shared<std::promise<Result>>();
    auto future = promise->get_future();

    // 立即拷贝文件路径：调用方可能传入临时字符串的 c_str()（如
    // dataPath("...").c_str()），其生命周期在表达式结束时即终止，
    // 后台线程必须持有自己的副本，否则将读悬空指针。
    std::string path(filepath ? filepath : "");

    worker_ = std::thread([this, path = std::move(path), progress = std::move(progress), promise]() {
        try {
            runStreaming(path, progress, promise);
        } catch (...) {
            // 兜底：后台线程绝不向外抛异常（否则 std::terminate）。
            // 任何未预期异常都按"失败结果"交付，保证 future 一定就绪。
            Result result;
            RowError fatal;
            fatal.line_number = 0;
            fatal.raw_line = "unexpected exception in streaming import";
            result.errors.push_back(std::move(fatal));
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.total_lines = hot_total_lines_.v.load(std::memory_order_relaxed);
                stats_.rows_imported = hot_rows_imported_.v.load(std::memory_order_relaxed);
                stats_.rows_skipped = hot_rows_skipped_.v.load(std::memory_order_relaxed);
                stats_.elapsed_ms = 0.0;
            }
            if (error_stream_.is_open()) error_stream_.close();
            streaming_active_.store(false, std::memory_order_relaxed);
            try {
                promise->set_value(std::move(result));
            } catch (...) {}
        }
    });

    return future;
}

// ============================================================
// runStreaming：后台线程主体
// ============================================================

void ImportPipeline::runStreaming(
    const std::string& filepath,
    std::function<bool(size_t)> progress,
    std::shared_ptr<std::promise<Result>> promise) {
    Result result;
    auto t0 = std::chrono::steady_clock::now();

    // 预扫描总行数：shutdown() 超时返回后 pendingCount() 需要"总行数 -
    // 已处理行数"来估算未完成任务。二进制逐块数 '\n'，只读不解析。
    total_lines_est_.store(countLines(filepath), std::memory_order_relaxed);
    total_known_.store(1, std::memory_order_relaxed);

    CsvParser parser(cfg_.parser);
    if (!parser.parse(filepath.c_str())) {
        // 文件打开或表头解析失败：与 importSync 一致，记录致命错误并交付
        RowError fatal;
        fatal.line_number = 0;
        fatal.raw_line = parser.lastError();
        result.errors.push_back(std::move(fatal));

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_ = Stats{};
        }
        streaming_active_.store(false, std::memory_order_relaxed);
        promise->set_value(std::move(result));
        return;
    }

    Row row;
    size_t processed = 0;
    while (!stop_requested_.load(std::memory_order_relaxed) && parser.readRow(row)) {
        hot_total_lines_.v.fetch_add(1, std::memory_order_relaxed);
        ++processed;

        try {
            // ---- 列数检查 ----
            if (row.size() != parser.header().size()) {
                std::ostringstream oss;
                oss << "Column count mismatch at line " << parser.lineNumber()
                    << ": expected " << parser.header().size()
                    << " columns, got " << row.size();
                throw std::runtime_error(oss.str());
            }

            // ---- 校验与收集 ----
            const bool valid = validateAndCollect(
                row, parser.lineNumber(), parser.lastRawLine(), result);
            if (!valid && !cfg_.skip_bad_rows) {
                break;
            }
        } catch (const std::runtime_error& e) {
            // 非跳过模式：importSync 在此抛异常；流式导入约定不抛，
            // 等价的语义是记录致命错误后停止导入，把已完成部分交出。
            if (!cfg_.skip_bad_rows) {
                RowError row_err;
                row_err.line_number = parser.lineNumber();
                row_err.raw_line = parser.lastRawLine();
                CellError ce;
                ce.column_index = 0;
                const std::string message(e.what());
                ce.code = message.rfind("Column count mismatch", 0) == 0
                              ? ErrorCode::ColumnCountMismatch
                              : ErrorCode::InternalError;
                ce.message = e.what();
                row_err.cell_errors.push_back(std::move(ce));
                collectError(row_err, result);
                break;
            }
            // 跳过模式：列数不匹配等整行级错误记录后继续，不中断导入
            ++result.rows_skipped;
            if (cfg_.error_limit == 0 || result.errors.size() < cfg_.error_limit) {
                RowError row_err;
                row_err.line_number = parser.lineNumber();

                row_err.raw_line = parser.lastRawLine();

                CellError ce;
                ce.column_index = 0;
                const std::string message(e.what());
                ce.code = message.rfind("Column count mismatch", 0) == 0
                              ? ErrorCode::ColumnCountMismatch
                              : ErrorCode::InternalError;
                ce.message = e.what();
                row_err.cell_errors.push_back(std::move(ce));
                collectError(row_err, result);
            }
        }

        // 热路径计数器镜像到原子（stats() 靠它们反映实时进度）
        hot_rows_imported_.v.store(result.rows_imported, std::memory_order_relaxed);
        hot_rows_skipped_.v.store(result.rows_skipped, std::memory_order_relaxed);

        // 每处理 1000 行调用一次进度回调；返回 false 表示调用方要求停止
        if (processed % 1000 == 0) {
            if (progress && !progress(processed)) {
                break;
            }
        }
    }

    // ---- 收尾：写回 stats_、复位活跃标志、交付结果 ----
    auto t1 = std::chrono::steady_clock::now();
    result.elapsed_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_lines = hot_total_lines_.v.load(std::memory_order_relaxed);
        stats_.rows_imported = hot_rows_imported_.v.load(std::memory_order_relaxed);
        stats_.rows_skipped = hot_rows_skipped_.v.load(std::memory_order_relaxed);
        stats_.bytes_read = parser.bytesRead();
        stats_.elapsed_ms = result.elapsed_ms;
    }

    if (error_stream_.is_open()) error_stream_.close();
    streaming_active_.store(false, std::memory_order_relaxed);
    promise->set_value(std::move(result));
}

// ============================================================
// countLines：二进制逐块数换行，估算文件总行数
// ============================================================

size_t ImportPipeline::countLines(const std::string& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in) return 0;
    size_t lines = 0;
    char buf[1 << 16];
    while (in) {
        in.read(buf, sizeof(buf));
        std::streamsize n = in.gcount();
        if (n == 0) break;
        for (std::streamsize i = 0; i < n; ++i) {
            if (buf[i] == '\n') ++lines;
        }
    }
    return lines;
}

// ============================================================
// shutdown
// ============================================================

void ImportPipeline::shutdown(std::chrono::milliseconds timeout) {
    // 没有正在进行的流式导入：无事可停
    if (!worker_.joinable()) {
        pending_count_.store(0, std::memory_order_relaxed);
        return;
    }

    // 1. 置位停止标志，通知后台线程收尾
    stop_requested_.store(true, std::memory_order_relaxed);

    // 2. 最多等 timeout 毫秒。后台线程收尾的最后一步会把 streaming_active_
    //    复位为 false，然后交付结果并退出线程；据此判断是否已停干净。
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (streaming_active_.load(std::memory_order_relaxed)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!streaming_active_.load(std::memory_order_relaxed)) {
        // 后台已收尾：取回线程（join 不阻塞），未完成任务数为 0
        worker_.join();
        pending_count_.store(0, std::memory_order_relaxed);
    } else {
        // 3. 超时：不强行结束线程，记录未完成任务数（估算），
        //    线程继续在后台收尾，最终由析构函数 join。
        size_t total = total_known_.load(std::memory_order_relaxed)
                           ? total_lines_est_.load(std::memory_order_relaxed)
                           : 0;
        size_t done = hot_total_lines_.v.load(std::memory_order_relaxed);
        pending_count_.store(total > done ? total - done : 0,
                             std::memory_order_relaxed);
    }
}

// ============================================================
// ~ImportPipeline
// ============================================================

ImportPipeline::~ImportPipeline() {
    if (worker_.joinable()) {
        stop_requested_.store(true, std::memory_order_relaxed);
        worker_.join();
    }
}

// ============================================================
// stats
// ============================================================

Stats ImportPipeline::stats() const {
    Stats s;
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        s = stats_;
    }
    // 热路径原子计数器反映"进行中的流式导入"实时进度（Part B 由后台线程更新并
    // 在结束时把最终值写入 stats_）。同步导入 importSync 直接把最终结果写进
    // stats_。原实现无条件用热原子覆盖 stats_，导致同步导入后统计被全零覆盖
    // 而归零。修复：仅当存在活跃的流式导入时才读取热原子。
    if (streaming_active_.load(std::memory_order_relaxed)) {
        s.total_lines = hot_total_lines_.v.load(std::memory_order_relaxed);
        s.rows_imported = hot_rows_imported_.v.load(std::memory_order_relaxed);
        s.rows_skipped = hot_rows_skipped_.v.load(std::memory_order_relaxed);
    }
    return s;
}

} // namespace csvp
