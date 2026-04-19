#pragma once

#include <tree_sitter/api.h>

#include <filesystem>
#include <ftxui/screen/color.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "HighlightTheme.hpp"

namespace ally::rendering {

/// Sentinel slot value indicating a capture should be skipped (e.g. @_parent).
inline constexpr uint8_t kSkipSlot = 255;

/// Default highlight priority (matches nvim-treesitter convention).
inline constexpr int kDefaultPriority = 100;

struct CompiledLang {
  const TSLanguage* language = nullptr;
  TSQuery* query = nullptr;
  std::vector<uint8_t> capture_to_slot;
  std::vector<ftxui::Color> slot_colors;
  std::vector<int> pattern_priority;  // per-pattern priority from #set! priority
};

class QueryStore {
 public:
  explicit QueryStore(HighlightTheme theme, std::vector<std::filesystem::path> query_dirs = {});
  ~QueryStore();

  QueryStore(const QueryStore&) = delete;
  auto operator=(const QueryStore&) -> QueryStore& = delete;

  /// Get a compiled language entry. Returns nullptr if the language is
  /// unknown or the .scm file could not be loaded/compiled.
  auto Get(const std::string& lang) -> CompiledLang*;

  /// Map a file path to a canonical language name based on its extension.
  /// Returns "" if the extension is not recognized.
  static auto LanguageFromPath(const std::string& path) -> std::string;

 private:
  HighlightTheme theme_;
  std::vector<std::filesystem::path> query_dirs_;
  std::unordered_map<std::string, CompiledLang> cache_;

  static auto Normalize(const std::string& lang) -> std::string;
  static auto ReadFile(const std::filesystem::path& path) -> std::string;
  auto ResolveQueryDir(const std::string& lang) -> std::filesystem::path;
  auto LoadQueryFromDir(const std::filesystem::path& base_dir, const std::string& lang, int depth) -> std::string;
  auto LoadQueryText(const std::string& canonical_lang) -> std::string;
  void Compile(CompiledLang& entry, const std::string& query_text);
};

}  // namespace ally::rendering
