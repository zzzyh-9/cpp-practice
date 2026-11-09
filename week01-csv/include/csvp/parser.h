#pragma once

#include "types.h"
#include "row.h"

#include <cstdio>
#include <string>
#include <vector>
#include <memory>
namespace csvp {

// CsvParser: 基于 fast-cpp-csv-parser 设计思路改编的 CSV 解析器。
// 职责：打开文件 → 按分隔符切分字段 → 逐行产出 Row。
// 设计简化了原库的模板策略（trim_policy/quote_policy），
// 改为 Config 驱动的运行时配置，更适应动态列数场景。
//
// 已知限制（README 中已注明）：
// - 单行最大长度 16 MB (2^24-1)
// - 引号转义仅支持双引号 ("")
// - 不支持跨行引号字段
// - 编码支持 UTF-8 和 GBK（GBK 路径未经充分测试）
class CsvParser {
public:
    struct Config {
        char delimiter = ',';
        bool has_header = true;
        bool trim_spaces = true;
        const char* encoding = "UTF-8";  // "UTF-8" 或 "GBK"
    };

    CsvParser() = default;
    explicit CsvParser(const Config& cfg);

    ~CsvParser();

    // 打开并解析文件。成功返回 true。
    // 注意：重复调用 parse() 会关闭前一个文件并打开新文件。
    bool parse(const char* filepath);

    // 获取表头（仅在 has_header=true 且 parse() 成功后有效）
    const std::vector<std::string>& header() const { return header_; }

    // 逐行读取。返回 false 表示文件结束或错误。
    // row 会被填充为下一行的字段列表。
    bool readRow(Row& row);

    // 元信息
    size_t lineNumber() const;    // 当前行号（从 1 开始；size_t 避免大文件行号溢出）
    size_t bytesRead() const;     // 已读取字节数
    const std::string& lastError() const { return last_error_; }
    const std::string& lastRawLine() const { return raw_line_; }

private:
    // 内部行缓冲区管理
    bool readLineFromFile();
    bool splitLineToFields(const char* line, size_t length, Row& row);
    bool handleQuotedField(const char* line, size_t& pos, size_t length, std::string& field);

    // 编码感知的字节分类
    bool isTrailingByte(unsigned char c) const;  // 判断是否为多字节字符的后续字节

    Config cfg_;
    std::string last_error_;
    std::string raw_line_;

    // ---- 文件 I/O ----
    FILE* file_ = nullptr;
    size_t file_size_ = 0;

    // ---- 行缓冲区 ----
    // 缓冲区使用手动内存管理（malloc/free）。
    // TODO: 考虑切换到 std::vector<char> 以简化内存管理。
    char* line_buffer_ = nullptr;
    size_t buffer_capacity_ = 0;  // 缓冲区容量（16 MB 行长度上限）
    size_t buffer_offset_ = 0;    // （保留：历史字段，流式块读已不再逐行使用）

    // ---- 流式块读缓冲（Part C C-2 性能修复）----
    // 原来每读一行都 fseek + fread 最多 16 MB，10 万行文件退化为 O(n²)
    // （每行重复复制大块缓冲）。改为 64 KB 块读 + 消费 + 跨块拼行：
    // 1 GB 文件只读约 16k 次块，行拷贝与文件大小线性。
    static constexpr size_t kReadBlock = 64 * 1024;
    std::vector<char> read_buf_;   // kReadBlock 字节
    size_t read_buf_pos_ = 0;      // 当前消费位置
    size_t read_buf_len_ = 0;      // 当前块有效字节数
    size_t line_accum_len_ = 0;    // line_buffer_ 中已累积的字节（跨块行）

    // ---- 解析状态 ----
    std::vector<std::string> header_;
    size_t line_number_ = 0; // 行号计数器
    size_t bytes_read_ = 0; // 已读字节
    bool header_parsed_ = false;
};

} // namespace csvp
