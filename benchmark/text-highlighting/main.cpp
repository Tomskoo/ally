#include <algorithm>
#include <chrono>
#include <cstring>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "plain_renderer.hpp"
#include "sample_data.hpp"
#include "treesitter_renderer.hpp"

using namespace ftxui;
using Clock = std::chrono::high_resolution_clock;

struct BenchResult {
  double min_ms;
  double median_ms;
  double mean_ms;
};

static Element RenderAll(ally::rendering::PlainRenderer& renderer, const std::string& markdown) {
  auto blocks = renderer.Render(markdown);
  Elements elems;
  elems.reserve(blocks.size());
  for (auto& b : blocks) {
    elems.push_back(std::move(b.element));
  }
  return vbox(std::move(elems));
}

static BenchResult Benchmark(ally::rendering::PlainRenderer& renderer, const std::string& markdown, int iterations) {
  std::vector<double> times;
  times.reserve(iterations);

  // Warmup
  for (int i = 0; i < 3; ++i) {
    auto element = RenderAll(renderer, markdown);
    auto screen = Screen::Create(Dimension::Fixed(120), Dimension::Fixed(60));
    Render(screen, element);
  }

  for (int i = 0; i < iterations; ++i) {
    auto start = Clock::now();
    auto element = RenderAll(renderer, markdown);
    auto screen = Screen::Create(Dimension::Fixed(120), Dimension::Fixed(60));
    Render(screen, element);
    auto end = Clock::now();

    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    times.push_back(ms);
  }

  std::sort(times.begin(), times.end());

  double min_ms = times.front();
  double median_ms = times[times.size() / 2];
  double mean_ms = std::accumulate(times.begin(), times.end(), 0.0) / times.size();

  return {min_ms, median_ms, mean_ms};
}

static void PrintTable(const std::vector<std::pair<std::string, std::pair<BenchResult, BenchResult>>>& results) {
  std::cout << "\n";
  std::cout << std::left << std::setw(12) << "Fixture"
            << " | " << std::right << std::setw(12) << "Plain (med)"
            << " | " << std::setw(12) << "TS (med)"
            << " | " << std::setw(8) << "Ratio"
            << " | " << std::setw(12) << "Plain (min)"
            << " | " << std::setw(12) << "TS (min)"
            << "\n";
  std::cout << std::string(78, '-') << "\n";

  for (const auto& [name, pair] : results) {
    const auto& [plain, ts] = pair;
    double ratio = ts.median_ms / std::max(plain.median_ms, 0.001);
    std::cout << std::left << std::setw(12) << name << " | " << std::right << std::setw(10) << std::fixed << std::setprecision(3)
              << plain.median_ms << "ms"
              << " | " << std::setw(10) << ts.median_ms << "ms"
              << " | " << std::setw(6) << std::setprecision(2) << ratio << "x"
              << " | " << std::setw(10) << std::setprecision(3) << plain.min_ms << "ms"
              << " | " << std::setw(10) << ts.min_ms << "ms"
              << "\n";
  }
  std::cout << "\n";
}

static void VisualDump(ally::rendering::PlainRenderer& renderer, const std::string& label, const std::string& markdown) {
  auto element = RenderAll(renderer, markdown);
  // Use a tall screen to avoid clipping. The actual content height is
  // determined by FTXUI layout; extra rows are just empty.
  auto screen = Screen::Create(Dimension::Fixed(120), Dimension::Fixed(500));
  Render(screen, element);

  // Trim trailing empty lines from the screen output.
  auto output = screen.ToString();
  std::cout << "\n=== " << label << " ===\n";
  std::cout << output << "\n";
}

int main(int argc, char* argv[]) {
  bool visual = false;
  int iterations = 100;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--visual") == 0) {
      visual = true;
    } else if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
      iterations = std::atoi(argv[++i]);
    }
  }

  ally::rendering::PlainRenderer plain;
  ally::rendering::TreeSitterRenderer treesitter;

  struct Fixture {
    std::string name;
    std::string content;
  };

  std::vector<Fixture> fixtures = {
      {"small", std::string(ally::benchmark::kSmallMarkdown)},
      {"medium", std::string(ally::benchmark::kMediumMarkdown)},
      {"large", std::string(ally::benchmark::kLargeMarkdown)},
  };

  if (visual) {
    for (const auto& f : fixtures) {
      VisualDump(plain, "Plain / " + f.name, f.content);
      VisualDump(treesitter, "TreeSitter / " + f.name, f.content);
    }
    return 0;
  }

  std::cout << "Running " << iterations << " iterations per fixture...\n";

  std::vector<std::pair<std::string, std::pair<BenchResult, BenchResult>>> results;
  for (const auto& f : fixtures) {
    std::cout << "  Benchmarking: " << f.name << "..." << std::flush;
    auto plain_result = Benchmark(plain, f.content, iterations);
    auto ts_result = Benchmark(treesitter, f.content, iterations);
    results.push_back({f.name, {plain_result, ts_result}});
    std::cout << " done\n";
  }

  PrintTable(results);
  return 0;
}
