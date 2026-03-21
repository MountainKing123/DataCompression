#pragma once
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>

// Benchmark result for a single test case
struct BenchmarkResult
{
    std::string name;
    size_t uncompressedSize = 0;
    size_t compressedSize = 0;
    double compressionTime_ms = 0.0;
    double decompressionTime_ms = 0.0;
    bool roundtrip = false;

    [[nodiscard]] double getRatio() const
    {
        return compressedSize > 0
            ? static_cast<double>(uncompressedSize) / static_cast<double>(compressedSize)
            : 0.0;
    }

    [[nodiscard]] double getCompressSpeed_MB_s() const
    {
        return compressionTime_ms > 0
            ? (static_cast<double>(uncompressedSize) / (1024.0 * 1024.0)) / (compressionTime_ms / 1000.0)
            : 0.0;
    }

    [[nodiscard]] double getDecompressSpeed_MB_s() const
    {
        return decompressionTime_ms > 0
            ? (static_cast<double>(uncompressedSize) / (1024.0 * 1024.0)) / (decompressionTime_ms / 1000.0)
            : 0.0;
    }
};

// Track benchmark results across runs and compute deltas
class BenchmarkTracker
{
public:
    static constexpr const char* BENCHMARK_DIR = "bench_results";

    BenchmarkTracker() = default;

    // Save current benchmark results to file
    static void saveResults(const std::string& filename,
                     const std::vector<BenchmarkResult>& results)
    {
        // Save to current working directory
        std::ofstream f(filename);
        if (!f)
        {
            std::cerr << "Warning: Could not open " << filename << " for writing\n";
            return;
        }

        // CSV header
        f << "Name,Uncompressed,Compressed,Ratio,CompressTime_ms,"
          << "DecompressTime_ms,CompressSpeed_MB_s,DecompressSpeed_MB_s,Roundtrip\n";

        // Data rows
        for (const auto& r : results)
        {
            f << r.name << ","
              << r.uncompressedSize << ","
              << r.compressedSize << ","
              << std::fixed << std::setprecision(2) << r.getRatio() << ","
              << std::fixed << std::setprecision(3) << r.compressionTime_ms << ","
              << std::fixed << std::setprecision(3) << r.decompressionTime_ms << ","
              << std::fixed << std::setprecision(1) << r.getCompressSpeed_MB_s() << ","
              << std::fixed << std::setprecision(1) << r.getDecompressSpeed_MB_s() << ","
              << (r.roundtrip ? "PASS" : "FAIL") << "\n";
        }

        f.close();
    }

    // Load previous benchmark results
    static std::vector<BenchmarkResult> loadResults(const std::string& filename)
    {
        std::vector<BenchmarkResult> results;
        std::ifstream f(filename);
        if (!f) return results;

        std::string line;
        std::getline(f, line); // Skip header

        while (std::getline(f, line))
        {
            if (line.empty()) continue;

            BenchmarkResult r;
            std::istringstream iss(line);
            std::string roundtrip_str;

            char delim = ',';
            std::getline(iss, r.name, delim);
            iss >> r.uncompressedSize >> delim
                >> r.compressedSize >> delim;

            // Parse the ratio (we'll recalculate it anyway)
            double ratio;
            iss >> ratio >> delim;

            iss >> r.compressionTime_ms >> delim
                >> r.decompressionTime_ms >> delim;

            // Parse speeds (we'll recalculate them anyway)
            double cspeed, dspeed;
            iss >> cspeed >> delim >> dspeed >> delim;

            std::getline(iss, roundtrip_str);
            r.roundtrip = (roundtrip_str == "PASS");

            results.push_back(r);
        }

        return results;
    }

    // Print comparison table (current vs previous)
    static void printDeltaReport(const std::vector<BenchmarkResult>& current,
                          const std::vector<BenchmarkResult>& previous)
    {
        std::cout << "\n=== Benchmark Delta Report ===\n";
        printDeltaReportImpl(current, previous, nullptr);
    }

    // Save comparison table to file
    static void saveDeltaReport(const std::string& filename,
                         const std::vector<BenchmarkResult>& current,
                         const std::vector<BenchmarkResult>& previous)
    {
        std::ofstream f(filename);
        if (!f)
        {
            std::cerr << "Warning: Could not open " << filename << " for writing\n";
            return;
        }
        printDeltaReportImpl(current, previous, &f);
        f.close();
    }

private:
    // Implementation for printing/saving delta report
    static void printDeltaReportImpl(const std::vector<BenchmarkResult>& current,
                             const std::vector<BenchmarkResult>& previous,
                             std::ofstream* file)
    {
        auto out = [&](const std::string& s) {
            if (file)
                *file << s << "\n";
            else
                std::cout << s << "\n";
        };

        std::string header = std::string(140, '=');
        std::string divider = std::string(140, '-');

        out(header);
        std::ostringstream oss;
        oss << std::left << std::setw(40) << "Test Case"
            << std::right
            << std::setw(12) << "Ratio"
            << std::setw(12) << "Delta %"
            << std::setw(14) << "Enc MB/s"
            << std::setw(12) << "Delta %"
            << std::setw(14) << "Dec MB/s"
            << std::setw(12) << "Delta %";
        out(oss.str());
        out(divider);

        for (const auto& c : current)
        {
            auto it = std::ranges::find_if(previous,
                                           [&](const BenchmarkResult& p)
                                           { return p.name == c.name; });

            if (it == previous.end())
            {
                std::ostringstream newoss;
                newoss << std::left << std::setw(40) << c.name
                       << " (NEW)";
                out(newoss.str());
                continue;
            }

            const auto& p = *it;
            double ratio_delta = ((c.getRatio() - p.getRatio()) / p.getRatio()) * 100.0;
            double enc_delta =
                ((c.getCompressSpeed_MB_s() - p.getCompressSpeed_MB_s()) /
                 p.getCompressSpeed_MB_s()) *
                100.0;
            double dec_delta =
                ((c.getDecompressSpeed_MB_s() - p.getDecompressSpeed_MB_s()) /
                 p.getDecompressSpeed_MB_s()) *
                100.0;

            std::ostringstream deltaoss;
            deltaoss << std::left << std::setw(40) << c.name
                     << std::right << std::fixed << std::setprecision(2)
                     << std::setw(12) << c.getRatio()
                     << std::setw(12) << (ratio_delta > 0 ? "+" : "") << ratio_delta << "%"
                     << std::setw(14) << c.getCompressSpeed_MB_s()
                     << std::setw(12) << (enc_delta > 0 ? "+" : "") << enc_delta << "%"
                     << std::setw(14) << c.getDecompressSpeed_MB_s()
                     << std::setw(12) << (dec_delta > 0 ? "+" : "") << dec_delta << "%";
            out(deltaoss.str());
        }

        out(header);
    }

public:

    // Summary statistics - with per-data-type averages
    static void printSummary(const std::vector<BenchmarkResult>& results)
    {
        // Group results by data type (store indices to avoid pointer-to-local issues)
        std::map<std::string, std::vector<size_t>> byType;
        int totalPassCount = 0;

        for (size_t idx = 0; idx < results.size(); ++idx)
        {
            const auto& r = results[idx];
            // Extract data type name (everything before the last space which is the size)
            std::string name = r.name;
            const size_t lastSpace = name.rfind(' ');
            const std::string dataType = (lastSpace != std::string::npos)
                ? name.substr(0, lastSpace)
                : name;

            byType[dataType].push_back(idx);
            if (r.roundtrip) ++totalPassCount;
        }

        std::cout << "\n=== Summary ===\n";
        std::cout << "Total tests: " << results.size() << ", Passed: " << totalPassCount << "\n\n";

        std::cout << "Averages by Data Type:\n";
        std::cout << std::string(100, '-') << "\n";
        std::cout << std::left << std::setw(35) << "Data Type"
                  << std::right
                  << std::setw(16) << "Avg Ratio"
                  << std::setw(16) << "Avg Enc MB/s"
                  << std::setw(16) << "Avg Dec MB/s"
                  << "\n";
        std::cout << std::string(100, '-') << "\n";

        for (const auto& [dataType, indices] : byType)
        {
            double typeAvgRatio    = 0.0;
            double typeAvgEncSpeed = 0.0;
            double typeAvgDecSpeed = 0.0;

            for (const size_t i : indices)
            {
                typeAvgRatio    += results[i].getRatio();
                typeAvgEncSpeed += results[i].getCompressSpeed_MB_s();
                typeAvgDecSpeed += results[i].getDecompressSpeed_MB_s();
            }

            if (!indices.empty())
            {
                const auto count = static_cast<double>(indices.size());
                typeAvgRatio    /= count;
                typeAvgEncSpeed /= count;
                typeAvgDecSpeed /= count;
            }

            std::cout << std::left << std::setw(35) << dataType
                      << std::right << std::fixed << std::setprecision(2)
                      << std::setw(16) << typeAvgRatio << ":1"
                      << std::setprecision(1)
                      << std::setw(16) << typeAvgEncSpeed
                      << std::setw(16) << typeAvgDecSpeed
                      << "\n";
        }

        std::cout << std::string(100, '-') << "\n";
    }
};







