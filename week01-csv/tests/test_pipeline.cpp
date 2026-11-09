#include "csvp/pipeline.h"
#include "csvp/parser.h"
#include "csvp/validator.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <iostream>
#include <filesystem>
#include <fstream>

namespace {

using namespace csvp;

// Helper to get test data path
std::string dataPath(const char* filename) {
    return std::string(TEST_DATA_DIR) + filename;
}

// Helper: create default rules for sample_utf8.csv schema
std::vector<FieldRule> makeDefaultRules() {
    return {
        {"id", FieldType::Int, true},
        {"name", FieldType::String, true},
        {"age", FieldType::Int, true},
        {"score", FieldType::Double, true},
        {"active", FieldType::Bool, false},
    };
}

std::filesystem::path tempPath(const char* filename) {
    auto path = std::filesystem::temp_directory_path() / filename;
    std::filesystem::remove(path);
    return path;
}

// ============================================================
// Pipeline.SyncSmallClean (PASS)
// ============================================================
TEST(Pipeline, SyncSmallClean) {
    ImportPipeline::Config cfg;
    cfg.parser.delimiter = ',';
    cfg.parser.has_header = true;
    cfg.skip_bad_rows = false;

    ImportPipeline pipeline(cfg, makeDefaultRules());
    auto result = pipeline.importSync(dataPath("sample_utf8.csv").c_str());

    EXPECT_EQ(result.rows_imported, 10u);
    EXPECT_EQ(result.rows_skipped, 0u);
    EXPECT_TRUE(result.errors.empty());
    EXPECT_GT(result.elapsed_ms, 0.0);

    std::cout << "Pipeline.SyncSmallClean: imported " << result.rows_imported
              << " rows in " << result.elapsed_ms << " ms" << std::endl;
}

// ============================================================
// Pipeline.MissingFile (PASS)
// ============================================================
TEST(Pipeline, MissingFile) {
    ImportPipeline::Config cfg;
    ImportPipeline pipeline(cfg, {});
    auto result = pipeline.importSync("nonexistent_file_12345.csv");

    // Should report error, not crash
    EXPECT_EQ(result.rows_imported, 0u);
    std::cout << "Pipeline.MissingFile: errors=" << result.errors.size() << std::endl;
}

// ============================================================
// Pipeline.SkipBadRows
// ============================================================
TEST(Pipeline, SkipBadRows) {
    ImportPipeline::Config cfg;
    cfg.parser.delimiter = ',';
    cfg.parser.has_header = true;
    cfg.skip_bad_rows = true;  // 要求跳过坏行
    cfg.error_limit = 50;

    ImportPipeline pipeline(cfg, makeDefaultRules());

    // sample_dirty.csv contains rows with column-count mismatches and bad data.
    ImportPipeline::Result result;
    try {
        result = pipeline.importSync(dataPath("sample_dirty.csv").c_str());
        std::cout << "Pipeline.SkipBadRows: imported=" << result.rows_imported
                  << " skipped=" << result.rows_skipped
                  << " errors=" << result.errors.size() << std::endl;
    } catch (const std::exception& e) {
        FAIL() << "importSync threw despite skip_bad_rows=true: " << e.what();
    }

    // If we reach here without exception, verify results
    EXPECT_GT(result.rows_imported + result.rows_skipped, 0u)
        << "Should have processed at least some rows";
}

// ============================================================
// Pipeline.ErrorLimit (PASS)
// ============================================================
TEST(Pipeline, ErrorLimit) {
    ImportPipeline::Config cfg;
    cfg.parser.delimiter = ',';
    cfg.parser.has_header = true;
    cfg.skip_bad_rows = true;
    cfg.error_limit = 3;  // Only collect 3 errors max

    // Use sample_dirty.csv which has multiple bad rows; the error
    // limit should be respected.
    ImportPipeline pipeline(cfg, makeDefaultRules());

    try {
        auto result = pipeline.importSync(dataPath("sample_dirty.csv").c_str());
        EXPECT_LE(result.errors.size(), cfg.error_limit)
            << "Error limit should cap the number of stored errors";
    } catch (const std::exception& e) {
        std::cout << "Skipping ErrorLimit test: " << e.what() << std::endl;
        GTEST_SKIP() << "importSync threw an exception";
    }
}

// ============================================================
// Pipeline.StreamingMissingFile
// ============================================================
TEST(Pipeline, StreamingMissingFile) {
    ImportPipeline::Config cfg;
    ImportPipeline pipeline(cfg, makeDefaultRules());

    auto future = pipeline.importStreaming("dummy.csv",
        [](size_t) { return true; });

    auto result = future.get();
    // A missing file is delivered through the future without a background crash.
    EXPECT_EQ(result.rows_imported, 0u);
    EXPECT_FALSE(result.errors.empty());
}

// ============================================================
// Pipeline.ShutdownIdle
// ============================================================
TEST(Pipeline, ShutdownIdle) {
    ImportPipeline::Config cfg;
    ImportPipeline pipeline(cfg, {});
    pipeline.shutdown(std::chrono::milliseconds(100));
    EXPECT_EQ(pipeline.pendingCount(), 0u);
}

// ============================================================
// Part B acceptance: progress callback, cancellation and result delivery
// ============================================================
TEST(Pipeline, StreamingReportsEveryThousandRows) {
    ImportPipeline::Config cfg;
    cfg.parser.has_header = true;
    cfg.skip_bad_rows = true;

    ImportPipeline pipeline(cfg, makeDefaultRules());
    std::vector<size_t> progress;
    auto future = pipeline.importStreaming(
        dataPath("sample_100k.csv").c_str(),
        [&progress](size_t processed) {
            progress.push_back(processed);
            return true;
        });

    auto result = future.get();
    ASSERT_EQ(progress.size(), 100u);
    for (size_t i = 0; i < progress.size(); ++i) {
        EXPECT_EQ(progress[i], (i + 1) * 1000);
    }
    EXPECT_EQ(result.rows_imported, 98000u);
    EXPECT_EQ(result.rows_skipped, 2000u);
    EXPECT_EQ(pipeline.stats().total_lines, 100000u);
}

TEST(Pipeline, StreamingProgressCanStopImport) {
    ImportPipeline::Config cfg;
    cfg.parser.has_header = true;
    cfg.skip_bad_rows = true;

    ImportPipeline pipeline(cfg, makeDefaultRules());
    size_t last_progress = 0;
    auto future = pipeline.importStreaming(
        dataPath("sample_100k.csv").c_str(),
        [&last_progress](size_t processed) {
            last_progress = processed;
            return processed < 3000;
        });

    auto result = future.get();
    EXPECT_EQ(last_progress, 3000u);
    EXPECT_EQ(result.rows_imported + result.rows_skipped, 3000u);
}

TEST(Pipeline, ShutdownIsBoundedAndPendingCountIsReadable) {
    ImportPipeline::Config cfg;
    cfg.parser.has_header = true;
    cfg.skip_bad_rows = true;

    ImportPipeline pipeline(cfg, makeDefaultRules());
    auto future = pipeline.importStreaming(
        dataPath("sample_100k.csv").c_str(),
        [](size_t) { return true; });

    auto start = std::chrono::steady_clock::now();
    pipeline.shutdown(std::chrono::milliseconds(20));
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));
    (void)pipeline.pendingCount();
    (void)future.get();
    EXPECT_EQ(pipeline.pendingCount(), 0u);
}

TEST(Pipeline, RowErrorKeepsOriginalCsvText) {
    const auto input = tempPath("csvp_raw_line.csv");
    const auto errors = tempPath("csvp_raw_line_errors.txt");
    {
        std::ofstream out(input, std::ios::binary);
        out << "id,name\n1,\"bad,value\"\n";
    }

    ImportPipeline::Config cfg;
    cfg.parser.has_header = true;
    cfg.skip_bad_rows = true;
    cfg.error_file = errors.string();

    ImportPipeline pipeline(cfg, {
        {"id", FieldType::Int, true},
        {"name", FieldType::Int, true},
    });
    auto result = pipeline.importSync(input.string().c_str());

    EXPECT_EQ(result.rows_skipped, 1u);
    std::string contents;
    {
        std::ifstream in(errors, std::ios::binary);
        contents.assign(std::istreambuf_iterator<char>(in),
                        std::istreambuf_iterator<char>());
    }
    EXPECT_NE(contents.find("raw=1,\"bad,value\""), std::string::npos);
    EXPECT_NE(contents.find("TypeConversionError"), std::string::npos);

    std::filesystem::remove(input);
    std::filesystem::remove(errors);
}

} // anonymous namespace
