#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "src/rendering/HighlightTheme.hpp"
#include "src/rendering/QueryStore.hpp"
#include "src/rendering/TreeSitterRenderer.hpp"

using namespace ftxui;
using Clock = std::chrono::high_resolution_clock;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Minimal chat rendering — mirrors the essential logic in StageView without
// pulling in the full view dependency graph.
// ---------------------------------------------------------------------------

static auto SplitLines(const std::string& str) -> std::vector<std::string> {
  std::vector<std::string> lines;
  std::istringstream stream(str);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  return lines;
}

static auto ExtractFileContent(const std::string& output) -> std::string {
  auto content_start = output.find("<content>");
  auto content_end = output.rfind("</content>");
  if (content_start == std::string::npos || content_end == std::string::npos) { return ""; }
  content_start += 9;  // strlen("<content>")
  auto raw = output.substr(content_start, content_end - content_start);

  std::string result;
  std::istringstream stream(raw);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.find("(End of file") == 0) { continue; }
    auto colon = line.find(": ");
    if (colon != std::string::npos && colon <= 6) {
      bool all_digits = true;
      for (size_t i = 0; i < colon; ++i) {
        if (line[i] < '0' || line[i] > '9') { all_digits = false; break; }
      }
      if (all_digits) { line = line.substr(colon + 2); }
    }
    if (!result.empty()) { result += '\n'; }
    result += line;
  }
  return result;
}

static auto FormatDuration(double seconds) -> std::string {
  char buf[32];
  if (seconds < 60.0) {
    std::snprintf(buf, sizeof(buf), "%.1fs", seconds);
  } else {
    int mins = static_cast<int>(seconds) / 60;
    double secs = seconds - mins * 60;
    std::snprintf(buf, sizeof(buf), "%dm %.1fs", mins, secs);
  }
  return buf;
}

static auto RenderSubAgentPart(const json& part) -> Element {
  auto tool_state = part.value("state", json::object());
  auto input = tool_state.value("input", json::object());
  auto title = tool_state.value("title", "");
  auto status = tool_state.value("status", "");

  auto subagent_type = input.value("subagent_type", "task");
  auto description = input.value("description", title);

  std::string type_label = subagent_type;
  if (!type_label.empty()) {
    type_label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(type_label[0])));
  }

  Element header = hbox({
      text(type_label + " Task") | bold,
      text(" \u2014 ") | dim,
      text(description),
  });

  Elements meta_parts;
  auto time_obj = tool_state.value("time", json::object());
  if (time_obj.contains("start") && time_obj.contains("end")) {
    double duration_s =
        static_cast<double>(time_obj["end"].get<int64_t>() - time_obj["start"].get<int64_t>()) / 1000.0;
    meta_parts.push_back(text(FormatDuration(duration_s)) | dim);
  }
  if (status != "completed") {
    meta_parts.push_back(text("Running...") | color(Color::Yellow));
  }

  Element meta_el = meta_parts.empty() ? emptyElement() : hbox(std::move(meta_parts));

  return hbox({
      text("\u2502 ") | color(Color::Blue),
      vbox({header, meta_el}),
  });
}

static auto RenderToolPart(const json& part, ally::rendering::TreeSitterRenderer& renderer) -> Element {
  auto tool_name = part.value("tool", "tool");

  if (tool_name == "task") {
    return RenderSubAgentPart(part);
  }

  auto tool_state = part.value("state", json::object());
  auto title = tool_state.value("title", "");
  auto status = tool_state.value("status", "");
  auto output = tool_state.value("output", "");
  auto input = tool_state.value("input", json::object());

  std::string header_text = title.empty() ? tool_name : title;
  Elements els;
  els.push_back(text("# " + header_text) | bold);

  if (input.contains("command") && input["command"].is_string()) {
    els.push_back(text("$ " + input["command"].get<std::string>()) | color(Color::GrayLight));
  } else if (input.contains("file_path") && input["file_path"].is_string()) {
    els.push_back(text("$ " + input["file_path"].get<std::string>()) | color(Color::GrayLight));
  } else if (input.contains("pattern") && input["pattern"].is_string()) {
    els.push_back(text("$ " + input["pattern"].get<std::string>()) | color(Color::GrayLight));
  }

  if (!output.empty()) {
    // Detect file path for syntax highlighting.
    std::string file_path;
    if (input.contains("filePath") && input["filePath"].is_string()) {
      file_path = input["filePath"].get<std::string>();
    } else if (input.contains("file_path") && input["file_path"].is_string()) {
      file_path = input["file_path"].get<std::string>();
    }
    auto lang = ally::rendering::QueryStore::LanguageFromPath(file_path);
    auto clean_code = !lang.empty() ? ExtractFileContent(output) : std::string{};

    constexpr int kCollapseThreshold = 8;
    constexpr int kCollapsePreviewLines = 4;
    auto lines = SplitLines(!clean_code.empty() ? clean_code : output);

    if (static_cast<int>(lines.size()) <= kCollapseThreshold) {
      if (!clean_code.empty()) {
        els.push_back(renderer.RenderCodeBlock(clean_code, lang));
      } else {
        els.push_back(paragraph(output));
      }
    } else {
      if (!clean_code.empty()) {
        std::string preview_code;
        for (int idx = 0; idx < kCollapsePreviewLines && idx < static_cast<int>(lines.size()); ++idx) {
          if (idx > 0) preview_code += "\n";
          preview_code += lines[idx];
        }
        els.push_back(renderer.RenderCodeBlock(preview_code, lang));
      } else {
        std::string preview;
        for (int idx = 0; idx < kCollapsePreviewLines && idx < static_cast<int>(lines.size()); ++idx) {
          if (idx > 0) preview += "\n";
          preview += lines[idx];
        }
        els.push_back(paragraph(preview));
      }
      els.push_back(text("\u2026") | dim);
      els.push_back(text("Click to expand") | dim);
    }
  } else if (status != "completed") {
    els.push_back(text("  Running...") | dim);
  }

  return vbox(std::move(els)) | border | color(Color::GrayLight) | xflex;
}

static auto RenderMessage(const json& msg, ally::rendering::TreeSitterRenderer& renderer) -> Element {
  auto info = msg.value("info", json::object());
  auto role = info.value("role", "unknown");
  std::string icon = (role == "user") ? "$ " : "  ";

  auto parts = msg.value("parts", json::array());
  Elements parts_elements;

  for (const auto& part : parts) {
    auto part_type = part.value("type", "");

    if (part_type == "step-start" || part_type == "step-finish" || part_type == "patch") {
      continue;
    }

    std::string content;
    if (part.contains("text") && part["text"].is_string()) {
      content = part["text"].get<std::string>();
    } else if (part.contains("content") && part["content"].is_string()) {
      content = part["content"].get<std::string>();
    }

    Element part_el;
    if (part_type == "text") {
      auto blocks = renderer.Render(content);
      Elements block_els;
      for (auto& block : blocks) {
        block_els.push_back(std::move(block.element));
      }
      part_el = vbox(std::move(block_els));
    } else if (part_type == "tool") {
      part_el = RenderToolPart(part, renderer);
    } else if (part_type == "tool-call") {
      auto name = part.value("name", "tool");
      part_el = text("> " + name) | color(Color::Blue);
    } else if (part_type == "tool-result") {
      part_el = paragraph(content) | dim;
    } else if (part_type == "reasoning" || part_type == "thinking") {
      part_el = paragraph(content) | dim;
    } else {
      part_el = paragraph(content);
    }

    parts_elements.push_back(hbox({
        text(icon) | dim,
        part_el | flex,
    }));
  }

  if (parts_elements.empty()) {
    parts_elements.push_back(text(icon) | dim);
  }

  return vbox(std::move(parts_elements));
}

static auto RenderAllMessages(const json& messages, ally::rendering::TreeSitterRenderer& renderer) -> Element {
  Elements els;
  for (const auto& msg : messages) {
    els.push_back(RenderMessage(msg, renderer));
    els.push_back(separator());
  }
  return vbox(std::move(els));
}

// ---------------------------------------------------------------------------
// Benchmark harness
// ---------------------------------------------------------------------------

struct BenchResult {
  double min_ms;
  double median_ms;
  double mean_ms;
};

static auto Benchmark(const json& messages, ally::rendering::TreeSitterRenderer& renderer, int iterations)
    -> BenchResult {
  std::vector<double> times;
  times.reserve(iterations);

  // Warmup.
  for (int i = 0; i < 3; ++i) {
    auto element = RenderAllMessages(messages, renderer);
    auto screen = Screen::Create(Dimension::Fixed(120), Dimension::Fixed(60));
    Render(screen, element);
  }

  for (int i = 0; i < iterations; ++i) {
    auto start = Clock::now();
    auto element = RenderAllMessages(messages, renderer);
    auto screen = Screen::Create(Dimension::Fixed(120), Dimension::Fixed(60));
    Render(screen, element);
    auto end = Clock::now();
    times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
  }

  std::sort(times.begin(), times.end());
  return {
      times.front(),
      times[times.size() / 2],
      std::accumulate(times.begin(), times.end(), 0.0) / static_cast<double>(times.size()),
  };
}

static void VisualDump(const json& messages, ally::rendering::TreeSitterRenderer& renderer) {
  auto element = RenderAllMessages(messages, renderer);
  auto screen = Screen::Create(Dimension::Fixed(120), Dimension::Fixed(500));
  Render(screen, element);

  std::cout << "\n=== Chat Session Rendering ===\n";
  std::cout << screen.ToString() << "\n";
}

static auto LoadSessionJson(const std::string& path) -> json {
  std::ifstream file(path);
  if (!file.is_open()) {
    std::cerr << "Error: cannot open " << path << "\n";
    return json::array();
  }
  return json::parse(file);
}

int main(int argc, char* argv[]) {
  bool visual = false;
  int iterations = 100;
  std::string session_path = "test/chat/data/session.json";

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--visual") == 0) {
      visual = true;
    } else if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
      iterations = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--session") == 0 && i + 1 < argc) {
      session_path = argv[++i];
    }
  }

  auto theme = ally::rendering::HighlightTheme::LoadDefault();
  ally::rendering::TreeSitterRenderer renderer(theme);

  auto messages = LoadSessionJson(session_path);
  std::cout << "Loaded " << messages.size() << " messages from " << session_path << "\n";

  // Count part types for summary.
  int text_parts = 0;
  int tool_parts = 0;
  int task_parts = 0;
  for (const auto& msg : messages) {
    for (const auto& part : msg.value("parts", json::array())) {
      auto ptype = part.value("type", "");
      if (ptype == "text") ++text_parts;
      else if (ptype == "tool") {
        ++tool_parts;
        if (part.value("tool", "") == "task") ++task_parts;
      }
    }
  }
  std::cout << "Parts: " << text_parts << " text, " << tool_parts << " tool (" << task_parts << " sub-agent)\n";

  if (visual) {
    VisualDump(messages, renderer);
    return 0;
  }

  std::cout << "\nRunning " << iterations << " iterations...\n";
  auto result = Benchmark(messages, renderer, iterations);

  std::cout << std::fixed << std::setprecision(3);
  std::cout << "\nResults:\n";
  std::cout << "  min:    " << result.min_ms << " ms\n";
  std::cout << "  median: " << result.median_ms << " ms\n";
  std::cout << "  mean:   " << result.mean_ms << " ms\n";

  return 0;
}
