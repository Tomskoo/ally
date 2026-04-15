#include "QueryStore.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "src/configuration/Paths.hpp"

extern "C" {
auto tree_sitter_cpp() -> const TSLanguage*;
auto tree_sitter_python() -> const TSLanguage*;
auto tree_sitter_javascript() -> const TSLanguage*;
auto tree_sitter_rust() -> const TSLanguage*;
}

namespace ally::rendering {

namespace {

auto LookupLanguage(const std::string& lang) -> const TSLanguage* {
  if (lang == "cpp") {
    return tree_sitter_cpp();
  }
  if (lang == "python") {
    return tree_sitter_python();
  }
  if (lang == "javascript") {
    return tree_sitter_javascript();
  }
  if (lang == "rust") {
    return tree_sitter_rust();
  }
  return nullptr;
}

}  // namespace

QueryStore::QueryStore(HighlightTheme theme) : theme_(std::move(theme)) {}

QueryStore::~QueryStore() {
  for (auto& [lang_key, entry] : cache_) {
    if (entry.query != nullptr) {
      ts_query_delete(entry.query);
    }
  }
}

auto QueryStore::Normalize(const std::string& lang) -> std::string {
  if (lang == "c++" || lang == "cc" || lang == "cxx") { return "cpp";
}
  if (lang == "py") { return "python";
}
  if (lang == "js") { return "javascript";
}
  if (lang == "rs") { return "rust";
}
  return lang;
}

auto QueryStore::LoadQueryText(const std::string& canonical_lang) -> std::string {
  auto path = configuration::queries_dir() / canonical_lang / "highlights.scm";
  if (!std::filesystem::exists(path)) {
    return "";
  }
  std::ifstream file(path);
  if (!file.is_open()) {
    return "";
  }
  std::ostringstream buf;
  buf << file.rdbuf();
  return buf.str();
}

void QueryStore::Compile(CompiledLang& entry, const std::string& query_text) {
  if ((entry.language == nullptr) || query_text.empty()) { return;
}

  uint32_t error_offset = 0;
  TSQueryError error_type = TSQueryErrorNone;
  entry.query =
      ts_query_new(entry.language, query_text.c_str(), static_cast<uint32_t>(query_text.size()), &error_offset, &error_type);

  if (entry.query == nullptr) {
    std::cerr << "ally: failed to compile highlight query at offset " << error_offset << " (error type " << error_type << ")\n";
    return;
  }

  uint32_t capture_count = ts_query_capture_count(entry.query);
  entry.slot_colors.push_back(theme_.fg());  // slot 0 = default/fg
  entry.capture_to_slot.resize(capture_count, 0);

  for (uint32_t cap = 0; cap < capture_count; ++cap) {
    uint32_t name_len = 0;
    const char* name = ts_query_capture_name_for_id(entry.query, cap, &name_len);
    auto col = theme_.Resolve(std::string_view(name, name_len));

    // Deduplicate colors into slots
    uint8_t slot = 0;
    bool found = false;
    for (uint8_t idx = 0; idx < entry.slot_colors.size(); ++idx) {
      if (entry.slot_colors[idx] == col) {
        slot = idx;
        found = true;
        break;
      }
    }
    if (!found) {
      slot = static_cast<uint8_t>(entry.slot_colors.size());
      entry.slot_colors.push_back(col);
    }
    entry.capture_to_slot[cap] = slot;
  }
}

auto QueryStore::LanguageFromPath(const std::string& path) -> std::string {
  auto dot = path.rfind('.');
  if (dot == std::string::npos) { return "";
}
  auto ext = path.substr(dot);
  if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".hpp" || ext == ".hxx" || ext == ".h") { return "cpp";
}
  if (ext == ".py" || ext == ".pyi") { return "python";
}
  if (ext == ".js" || ext == ".mjs" || ext == ".cjs" || ext == ".jsx") { return "javascript";
}
  if (ext == ".rs") { return "rust";
}
  return "";
}

auto QueryStore::Get(const std::string& lang) -> CompiledLang* {
  auto canonical = Normalize(lang);

  auto iter = cache_.find(canonical);
  if (iter != cache_.end()) {
    return (iter->second.query != nullptr) ? &iter->second : nullptr;
  }

  // Not cached yet — try to load and compile
  const TSLanguage* ts_lang = LookupLanguage(canonical);
  if (ts_lang == nullptr) { return nullptr;
}

  auto query_text = LoadQueryText(canonical);
  if (query_text.empty()) { return nullptr;
}

  auto& entry = cache_[canonical];
  entry.language = ts_lang;
  Compile(entry, query_text);

  return (entry.query != nullptr) ? &entry : nullptr;
}

}  // namespace ally::rendering
