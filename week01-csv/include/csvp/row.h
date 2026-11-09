#pragma once

#include "types.h"

#include <string>
#include <vector>
#include <optional>
#include <stdexcept>
#include <cstdlib>

namespace csvp {

// Row: 一行 CSV 数据的轻量容器。
// 存储解析后的字段（全部为字符串），提供类型转换访问器。
class Row {
public:
    Row() = default;

    explicit Row(std::vector<std::string> fields)
        : fields_(std::move(fields)) {}

    // ---------- raw access ----------
    size_t size() const { return fields_.size(); }
    bool empty() const { return fields_.empty(); }

    const std::string& operator[](size_t index) const {
        return fields_[index];
    }

    std::string& operator[](size_t index) {
        return fields_[index];
    }

    const std::string& at(size_t index) const {
        return fields_.at(index);
    }

    // ---------- typed accessors ----------
    int toInt(size_t index) const {
        if (index >= fields_.size()) return 0;
        const auto& s = fields_[index];
        if (s.empty()) return 0;
        try {
            return std::stoi(s);
        } catch (...) {
            return 0;
        }
    }

    double toDouble(size_t index) const {
        if (index >= fields_.size()) return 0.0;
        const auto& s = fields_[index];
        if (s.empty()) return 0.0;
        return std::atof(s.c_str());
    }

    // ---------- 带解析状态的访问器（#5 修复）----------
    // 原 toInt/toDouble 对非法值返回 0 / 0.0，调用方无法区分"真 0 与解析失败"。
    // 新增 tryInt/tryDouble：解析成功返回值，越界/空/非法/含残留字符返回 nullopt。
    std::optional<int> tryInt(size_t index) const {
        if (index >= fields_.size()) return std::nullopt;
        const auto& s = fields_[index];
        if (s.empty()) return std::nullopt;
        try {
            std::size_t used = 0;
            const int v = std::stoi(s, &used);
            if (used != s.size()) return std::nullopt;  // 存在无法解析的剩余字符（如 "123abc"）
            return v;
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<double> tryDouble(size_t index) const {
        if (index >= fields_.size()) return std::nullopt;
        const auto& s = fields_[index];
        if (s.empty()) return std::nullopt;
        try {
            std::size_t used = 0;
            const double v = std::stod(s, &used);
            if (used != s.size()) return std::nullopt;
            return v;
        } catch (...) {
            return std::nullopt;
        }
    }

    const std::string& toString(size_t index) const {
        static const std::string empty;
        if (index >= fields_.size()) return empty;
        return fields_[index];
    }

    bool toBool(size_t index) const {
        if (index >= fields_.size()) return false;
        const auto& s = fields_[index];
        return s == "true" || s == "TRUE" || s == "1" || s == "yes";
    }

    // ---------- internal ----------
    void append(const std::string& field) { fields_.push_back(field); }
    void clear() { fields_.clear(); is_valid_ = true; }
    bool isValid() const { return is_valid_; }
    void setValid(bool v) { is_valid_ = v; }

private:
    std::vector<std::string> fields_;
    bool is_valid_ = true;
};

} // namespace csvp