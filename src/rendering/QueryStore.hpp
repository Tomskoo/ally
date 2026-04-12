#pragma once

#include <tree_sitter/api.h>

#include <ftxui/screen/color.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "HighlightTheme.hpp"

namespace ally::rendering {

struct CompiledLang {
  const TSLanguage* language = nullptr;
  TSQuery* query = nullptr;
  std::vector<uint8_t> capture_to_slot;
  std::vector<ftxui::Color> slot_colors;
};

class QueryStore {
 public:
  explicit QueryStore(HighlightTheme theme);
  ~QueryStore();

  QueryStore(const QueryStore&) = delete;
  auto operator=(const QueryStore&) -> QueryStore& = delete;

  /// Get a compiled language entry. Returns nullptr if the language is
  /// unknown or the .scm file could not be loaded/compiled.
  auto Get(const std::string& lang) -> CompiledLang*;

 private:
  HighlightTheme theme_;
  std::unordered_map<std::string, CompiledLang> cache_;

  static auto Normalize(const std::string& lang) -> std::string;
  static auto LoadQueryText(const std::string& canonical_lang) -> std::string;
  void Compile(CompiledLang& entry, const std::string& query_text);
};

}  // namespace ally::rendering
