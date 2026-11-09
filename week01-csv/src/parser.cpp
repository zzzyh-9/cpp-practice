#include "csvp/parser.h"

#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace csvp {

// ============================================================
// internal helpers
// ============================================================

static size_t getFileSize(FILE* f) {
#ifdef _WIN32
    auto fd = _fileno(f);
    return static_cast<size_t>(_filelengthi64(fd));
#else
    struct stat st;
    fstat(fileno(f), &st);
    return static_cast<size_t>(st.st_size);
#endif
}

static FILE* openBinaryFile(const char* filepath) {
#ifdef _WIN32
    if (!filepath || *filepath == '\0') return nullptr;

    // fopen() uses the process ANSI code page on Windows. Convert the UTF-8
    // path used by the public API so paths containing Chinese characters work.
    const int wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                             filepath, -1, nullptr, 0);
    if (wide_len <= 0) return nullptr;

    std::vector<wchar_t> wide_path(static_cast<size_t>(wide_len));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filepath, -1,
                            wide_path.data(), wide_len) <= 0) {
        return nullptr;
    }

    FILE* file = nullptr;
    if (_wfopen_s(&file, wide_path.data(), L"rb") != 0) return nullptr;
    return file;
#else
    return filepath ? std::fopen(filepath, "rb") : nullptr;
#endif
}

// ============================================================
// CsvParser
// ============================================================

CsvParser::CsvParser(const Config& cfg) : cfg_(cfg) {}

CsvParser::~CsvParser() {
    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }
    free(line_buffer_);
    line_buffer_ = nullptr;
}

// ---------- parse ----------
bool CsvParser::parse(const char* filepath) {
    // 关闭之前打开的文件
    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }

    // 释放之前分配的缓冲区（重复 parse() 时防止内存泄漏）
    free(line_buffer_);
    line_buffer_ = nullptr;

    // 打开文件
    file_ = openBinaryFile(filepath);
    if (!file_) {
        last_error_ = std::string("Failed to open file: ") + (filepath ? filepath : "");
        return false;
    }

    file_size_ = getFileSize(file_);

    // 分配行缓冲区（最大 16 MB）
    buffer_capacity_ = (1 << 24);  // 16 MB
    line_buffer_ = static_cast<char*>(malloc(buffer_capacity_));
    if (!line_buffer_) {
        // 分配失败时关闭已打开的文件，避免句柄泄漏
        fclose(file_);
        file_ = nullptr;
        last_error_ = "Failed to allocate line buffer";
        return false;
    }

    // ---- 状态重置 ----
    header_.clear();
    header_parsed_ = false;
    last_error_.clear();
    raw_line_.clear();
    buffer_offset_ = 0;
    line_number_ = 0;   // 重复 parse() 时行号从 0 重新计数（原来未重置）
    bytes_read_ = 0;    // 重复 parse() 时字节计数归零（原来未重置）

    // ---- 流式块读缓冲初始化 ----
    read_buf_.resize(kReadBlock);
    read_buf_pos_ = 0;
    read_buf_len_ = 0;
    line_accum_len_ = 0;

    // 解析表头
    if (cfg_.has_header) {
        Row header_row;
        if (!readRow(header_row)) {
            last_error_ = "Failed to read header row";
            return false;
        }
        for (size_t i = 0; i < header_row.size(); ++i) {
            header_.push_back(header_row[i]);
        }
        header_parsed_ = true;
    }

    return true;
}

// ---------- readRow ----------
bool CsvParser::readRow(Row& row) {
    if (!file_) {
        last_error_ = "No file opened";
        return false;
    }

    // 跳过空行用循环而非递归：连续大量空行时递归会栈溢出
    for (;;) {
        row.clear();

        // 读取下一行到 line_buffer_
        if (!readLineFromFile()) {
            return false;
        }

        size_t line_len = strlen(line_buffer_);
        // 行尾 \r 的剥离已由 readLineFromFile() 完成：
        // 只剥属于 CRLF 行尾的 \r，字段内容中的独立 \r 会保留。

        if (line_len == 0) {
            ++line_number_;
            continue;  // 空行：跳过，继续下一行
        }

        // 分割字段
        if (!splitLineToFields(line_buffer_, line_len, row)) {
            ++line_number_;
            return false;
        }
        ++line_number_;
        return true;
    }
}

// ---------- readLineFromFile ----------
// 流式块读实现（Part C C-2）：
// 以 64 KB 块为单位顺序读入 read_buf_，消费其中的行；行跨越块边界时
// 把当前缓冲剩余部分拼入 line_buffer_（16 MB 上限）再读下一块。
// 行为与原实现完全一致：CRLF 行尾 \r 剥离、EOF 残行交付、超长行报错。
bool CsvParser::readLineFromFile() {
    for (;;) {
        // 读缓冲耗尽：填充下一块
        if (read_buf_pos_ >= read_buf_len_) {
            read_buf_len_ = fread(read_buf_.data(), 1, kReadBlock, file_);
            read_buf_pos_ = 0;
            if (read_buf_len_ == 0) {
                // EOF：若累积了无换行符的残行（文件最后一行），交付之
                if (line_accum_len_ > 0) {
                    line_buffer_[line_accum_len_] = '\0';
                    raw_line_.assign(line_buffer_, line_accum_len_);
                    bytes_read_ += line_accum_len_;
                    line_accum_len_ = 0;
                    return true;
                }
                return false;
            }
        }

        // 在当前块剩余部分中查找 '\n'
        char* nl = static_cast<char*>(
            memchr(read_buf_.data() + read_buf_pos_, '\n',
                   read_buf_len_ - read_buf_pos_));
        if (nl) {
            size_t seg = static_cast<size_t>(nl - (read_buf_.data() + read_buf_pos_));
            if (line_accum_len_ + seg > buffer_capacity_ - 1) {
                last_error_ = "Line too long (>= 16MB): no newline within buffer capacity";
                return false;
            }
            memcpy(line_buffer_ + line_accum_len_, read_buf_.data() + read_buf_pos_, seg);
            line_accum_len_ += seg;
            line_buffer_[line_accum_len_] = '\0';

            // CRLF 行尾：仅当行以 \r 结尾时剥掉该 \r（行尾分隔符）。
            // 字段内容中的独立 \r（如 "has_cr\r" 后接 \n）不剥（A-10）。
            size_t len = line_accum_len_;
            if (len > 0 && line_buffer_[len - 1] == '\r') {
                --len;
            }
            raw_line_.assign(line_buffer_, len);
            line_buffer_[len] = '\0';

            // 推进读缓冲：跳过 '\n'（行字节 + 行尾符计入 bytes_read_）
            bytes_read_ += len + 1 + (line_accum_len_ - len);
            read_buf_pos_ = static_cast<size_t>(nl - read_buf_.data()) + 1;
            line_accum_len_ = 0;
            return true;
        }

        // 本块无 '\n'：整块剩余追加为行累积，继续读下一块
        size_t seg = read_buf_len_ - read_buf_pos_;
        if (line_accum_len_ + seg > buffer_capacity_ - 1) {
            last_error_ = "Line too long (>= 16MB): no newline within buffer capacity";
            return false;
        }
        memcpy(line_buffer_ + line_accum_len_, read_buf_.data() + read_buf_pos_, seg);
        line_accum_len_ += seg;
        read_buf_pos_ = read_buf_len_;
    }
}

// ---------- splitLineToFields ----------
bool CsvParser::splitLineToFields(const char* line, size_t length, Row& row) {
    row.clear();

    size_t pos = 0;
    std::string field;
    field.reserve(256);

    while (pos < length) {
        char c = line[pos];

        // 跳过前导空格（trim_spaces 模式）
        if (cfg_.trim_spaces && (c == ' ' || c == '\t') && field.empty()) {
            ++pos;
            continue;
        }

        // 引号字段处理
        if (c == '"') {
            if (!handleQuotedField(line, pos, length, field)) {
                return false;
            }
            // 跳过引号字段后的空格
            while (pos < length && cfg_.trim_spaces && (line[pos] == ' ' || line[pos] == '\t')) {
                ++pos;
            }
            bool consumed_delimiter = false;
            // 期望分隔符或行尾
            if (pos < length && line[pos] != cfg_.delimiter) {
                // 引号字段后面应该是分隔符或行尾
                last_error_ = "Unexpected character after quoted field";
                return false;
            }
            if (pos < length) {
                ++pos;  // 跳过分隔符
                consumed_delimiter = true;
            }
            row.append(field);
            field.clear();
            if (pos >= length && !consumed_delimiter) {
                // The quoted field was the final field. It has already been
                // appended, so do not let the generic tail path append a
                // second empty field.
                return true;
            }
            continue;
        }

        // 普通字段：读到分隔符或行尾
        if (c == cfg_.delimiter) {
            row.append(field);
            field.clear();
            ++pos;
            continue;
        }

        // 常规字符
        field += c;
        ++pos;
        // GBK 双字节字符：首字节 0x81~0xFE 时，第二字节（即便外观是分隔符/引号）
        // 一并归入字段，避免"双字节字符的第二字节恰好等于分隔符"时被误切分。
        if (cfg_.encoding && std::strcmp(cfg_.encoding, "GBK") == 0 &&
            (static_cast<unsigned char>(c) >= 0x81 && static_cast<unsigned char>(c) <= 0xFE) &&
            pos < length) {
            field += line[pos];
            ++pos;
        }
    }

    // 最后一个字段
    if (!field.empty() || pos > 0) {
        // 尾部空格修剪
        if (cfg_.trim_spaces) {
            while (!field.empty() && (field.back() == ' ' || field.back() == '\t')) {
                field.pop_back();
            }
        }
        row.append(field);
    }

    return true;
}

// ---------- handleQuotedField ----------
bool CsvParser::handleQuotedField(const char* line, size_t& pos, size_t length, std::string& field) {
    // 跳过开始的引号
    ++pos;  // 跳过第一个 '"'

    while (pos < length) {
        char c = line[pos];

        if (c == '"') {
            // 两个连续引号 "" 表示转义的字面引号
            if (pos + 1 < length && line[pos + 1] == '"') {
                field += '"';
                pos += 2;
                continue;
            }
            ++pos;  // 闭合引号，结束字段
            return true;
        } else {
            field += c;
            ++pos;
            // GBK 双字节字符：第二字节（即便外观是 '"'）一并归入字段
            if (cfg_.encoding && std::strcmp(cfg_.encoding, "GBK") == 0 &&
                (static_cast<unsigned char>(c) >= 0x81 && static_cast<unsigned char>(c) <= 0xFE) &&
                pos < length) {
                field += line[pos];
                ++pos;
            }
        }
    }

    // 引号未闭合
    last_error_ = "Unclosed quote in CSV field";
    return false;
}

// ---------- isTrailingByte ----------
bool CsvParser::isTrailingByte(unsigned char c) const {
    // UTF-8 后续字节范围：0x80 ~ 0xBF
    return (c >= 0x80 && c <= 0xBF);
}

// ---------- accessors ----------
size_t CsvParser::lineNumber() const {
    return line_number_;
}

size_t CsvParser::bytesRead() const {
    return bytes_read_;
}

} // namespace csvp
