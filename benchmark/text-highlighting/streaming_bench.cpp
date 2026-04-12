#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "plain_renderer.hpp"
#include "sample_data.hpp"
#include "treesitter_renderer.hpp"

using namespace ftxui;
using Clock = std::chrono::high_resolution_clock;

// Simulates a chatbot streaming text by growing the markdown string one chunk
// at a time and re-rendering after each chunk.  This measures the cost that
// users actually feel: every new token triggers a full parse + render cycle.

// ---------------------------------------------------------------------------
// Chunking helpers
// ---------------------------------------------------------------------------

// Split text at newline boundaries into chunks of roughly `target_chars` each.
// Never breaks mid-line so the markdown stays valid at every step.
static std::vector<std::string> ChunkByLines(const std::string& text, size_t target_chars) {
  std::vector<std::string> chunks;
  std::string current;
  size_t pos = 0;
  while (pos < text.size()) {
    auto nl = text.find('\n', pos);
    if (nl == std::string::npos) nl = text.size();
    auto line = text.substr(pos, nl - pos + (nl < text.size() ? 1 : 0));
    current += line;
    if (current.size() >= target_chars) {
      chunks.push_back(current);
      current.clear();
    }
    pos = nl + 1;
  }
  if (!current.empty()) chunks.push_back(current);
  return chunks;
}

// ---------------------------------------------------------------------------
// Streaming benchmark
// ---------------------------------------------------------------------------

struct StreamResult {
  int num_steps;          // how many incremental renders
  double total_ms;        // wall-clock for entire stream
  double mean_step_ms;    // mean per-step render time
  double median_step_ms;  // median per-step render time
  double p95_step_ms;     // 95th percentile per-step render time
  double max_step_ms;     // worst single step
};

static Element RenderAll(ally::rendering::PlainRenderer& renderer, const std::string& markdown) {
  auto blocks = renderer.Render(markdown);
  Elements elems;
  elems.reserve(blocks.size());
  for (auto& b : blocks) elems.push_back(std::move(b.element));
  return vbox(std::move(elems));
}

static StreamResult RunStreaming(ally::rendering::PlainRenderer& renderer, const std::string& full_text, size_t chunk_size) {
  auto chunks = ChunkByLines(full_text, chunk_size);

  // Warmup — render the full doc once
  {
    auto el = RenderAll(renderer, full_text);
    auto scr = Screen::Create(Dimension::Fixed(120), Dimension::Fixed(60));
    Render(scr, el);
  }

  std::vector<double> step_times;
  step_times.reserve(chunks.size());

  std::string accumulated;
  auto stream_start = Clock::now();

  for (const auto& chunk : chunks) {
    accumulated += chunk;

    auto t0 = Clock::now();
    auto element = RenderAll(renderer, accumulated);
    auto screen = Screen::Create(Dimension::Fixed(120), Dimension::Fixed(60));
    Render(screen, element);
    auto t1 = Clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    step_times.push_back(ms);
  }

  auto stream_end = Clock::now();
  double total_ms = std::chrono::duration<double, std::milli>(stream_end - stream_start).count();

  std::sort(step_times.begin(), step_times.end());
  int n = static_cast<int>(step_times.size());

  return {
      n,
      total_ms,
      std::accumulate(step_times.begin(), step_times.end(), 0.0) / n,
      step_times[n / 2],
      step_times[std::min(n - 1, static_cast<int>(n * 0.95))],
      step_times.back(),
  };
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

static void PrintHeader() {
  std::cout << "\n"
            << std::left << std::setw(12) << "Fixture"
            << " | " << std::setw(10) << "Renderer"
            << " | " << std::right << std::setw(6) << "Steps"
            << " | " << std::setw(10) << "Total"
            << " | " << std::setw(10) << "Mean"
            << " | " << std::setw(10) << "Median"
            << " | " << std::setw(10) << "p95"
            << " | " << std::setw(10) << "Max"
            << "\n";
  std::cout << std::string(96, '-') << "\n";
}

static void PrintRow(const std::string& fixture, const std::string& renderer, const StreamResult& r) {
  std::cout << std::left << std::setw(12) << fixture << " | " << std::setw(10) << renderer << " | " << std::right << std::setw(6)
            << r.num_steps << " | " << std::setw(8) << std::fixed << std::setprecision(2) << r.total_ms << "ms"
            << " | " << std::setw(8) << r.mean_step_ms << "ms"
            << " | " << std::setw(8) << r.median_step_ms << "ms"
            << " | " << std::setw(8) << r.p95_step_ms << "ms"
            << " | " << std::setw(8) << r.max_step_ms << "ms"
            << "\n";
}

// ---------------------------------------------------------------------------
// Fullscreen streaming — redraws in-place like a real chat UI
// ---------------------------------------------------------------------------

static std::pair<int, int> TerminalSize() {
  struct winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
    return {ws.ws_col, ws.ws_row};
  }
  return {120, 40};
}

static void VisualStream(ally::rendering::PlainRenderer& renderer, const std::string& label, const std::string& full_text,
                         size_t chunk_size, int delay_ms) {
  auto chunks = ChunkByLines(full_text, chunk_size);
  auto [cols, rows] = TerminalSize();

  // Enter alternate screen buffer + hide cursor
  std::cout << "\033[?1049h" << "\033[?25l" << std::flush;

  std::string accumulated;
  int step = 0;
  for (const auto& chunk : chunks) {
    accumulated += chunk;
    ++step;

    // Status bar takes 1 row
    int content_rows = rows - 1;

    auto content = RenderAll(renderer, accumulated);
    // focus at the bottom so yframe auto-scrolls to newest content
    auto scrollable = vbox({
                          content,
                          text("") | focus,
                      }) |
                      yframe | vscroll_indicator;

    auto screen = Screen::Create(Dimension::Fixed(cols), Dimension::Fixed(content_rows));
    Render(screen, scrollable);

    // Move cursor home and draw
    std::cout << "\033[H";

    // Status line
    std::string status = " " + label + " — step " + std::to_string(step) + "/" + std::to_string(chunks.size()) + " (" +
                         std::to_string(accumulated.size()) + " bytes)";
    status.resize(static_cast<size_t>(cols), ' ');
    std::cout << "\033[7m" << status << "\033[0m\n";

    std::cout << screen.ToString() << std::flush;

    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
  }

  // Hold for a moment, then restore
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  std::cout << "\033[?25h" << "\033[?1049l" << std::flush;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
  bool visual = false;
  size_t chunk_size = 80;                      // ~one line of chat output per chunk
  int delay_ms = 50;                           // ms between frames in visual mode
  std::string visual_renderer = "treesitter";  // or "plain"

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--visual") == 0) {
      visual = true;
    } else if (std::strcmp(argv[i], "--chunk-size") == 0 && i + 1 < argc) {
      chunk_size = static_cast<size_t>(std::atoi(argv[++i]));
    } else if (std::strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
      delay_ms = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--renderer") == 0 && i + 1 < argc) {
      visual_renderer = argv[++i];
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
    ally::rendering::PlainRenderer& renderer = (visual_renderer == "plain")
                                                   ? static_cast<ally::rendering::PlainRenderer&>(plain)
                                                   : static_cast<ally::rendering::PlainRenderer&>(treesitter);
    std::string label = (visual_renderer == "plain" ? "Plain" : "TreeSitter");

    for (const auto& f : fixtures) {
      VisualStream(renderer, label + " / " + f.name, f.content, chunk_size, delay_ms);
    }
    return 0;
  }

  std::cout << "Streaming benchmark (chunk size: ~" << chunk_size << " bytes)\n";
  std::cout << "Each step = accumulate chunk + full parse + render\n";

  PrintHeader();
  for (const auto& f : fixtures) {
    std::cout << std::flush;
    auto plain_res = RunStreaming(plain, f.content, chunk_size);
    auto ts_res = RunStreaming(treesitter, f.content, chunk_size);
    PrintRow(f.name, "Plain", plain_res);
    PrintRow(f.name, "TreeSitter", ts_res);
    std::cout << std::string(96, '-') << "\n";
  }
  std::cout << "\n";

  return 0;
}
