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

QueryStore::QueryStore(HighlightTheme theme, std::vector<std::filesystem::path> query_dirs)
    : theme_(std::move(theme)), query_dirs_(std::move(query_dirs)) {}

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

auto QueryStore::ReadFile(const std::filesystem::path& path) -> std::string {
  std::ifstream file(path);
  if (!file.is_open()) {
    return "";
  }
  std::ostringstream buf;
  buf << file.rdbuf();
  return buf.str();
}

auto QueryStore::ResolveQueryDir(const std::string& lang) -> std::filesystem::path {
  // Search default dir first, then configured dirs. Last match wins.
  std::filesystem::path resolved;

  auto default_path = configuration::queries_dir() / lang / "highlights.scm";
  if (std::filesystem::exists(default_path)) {
    resolved = default_path.parent_path().parent_path();
  }

  for (const auto& dir : query_dirs_) {
    auto path = dir / lang / "highlights.scm";
    if (std::filesystem::exists(path)) {
      resolved = dir;
    }
  }

  return resolved;
}

auto QueryStore::LoadQueryFromDir(const std::filesystem::path& base_dir, const std::string& lang,
                                  int depth) -> std::string {
  constexpr int kMaxInheritDepth = 5;
  if (depth > kMaxInheritDepth || base_dir.empty()) {
    return "";
  }

  auto path = base_dir / lang / "highlights.scm";
  auto content = ReadFile(path);
  if (content.empty()) {
    return "";
  }

  // Parse "; inherits: lang1,lang2" directive (nvim-treesitter convention).
  // Load parent queries from the same base directory and prepend them.
  std::string result;
  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line)) {
    auto pos = line.find("; inherits:");
    if (pos == std::string::npos) {
      continue;
    }
    auto parents_str = line.substr(pos + 11);  // skip "; inherits:"
    std::istringstream parents(parents_str);
    std::string parent;
    while (std::getline(parents, parent, ',')) {
      // Trim whitespace
      auto start = parent.find_first_not_of(" \t");
      auto end = parent.find_last_not_of(" \t");
      if (start == std::string::npos) {
        continue;
      }
      parent = parent.substr(start, end - start + 1);
      result += LoadQueryFromDir(base_dir, parent, depth + 1);
      result += "\n";
    }
    break;  // Only process the first inherits directive
  }

  result += content;
  return result;
}

auto QueryStore::LoadQueryText(const std::string& canonical_lang) -> std::string {
  auto base_dir = ResolveQueryDir(canonical_lang);
  return LoadQueryFromDir(base_dir, canonical_lang, 0);
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

    // Skip internal and metadata captures used by nvim-treesitter
    std::string_view cap_name(name, name_len);
    if ((name_len > 0 && name[0] == '_') ||
        cap_name == "spell" || cap_name == "nospell" ||
        cap_name == "conceal" || cap_name == "fold") {
      entry.capture_to_slot[cap] = kSkipSlot;
      continue;
    }

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

  // Extract per-pattern priorities from #set! priority directives
  uint32_t pattern_count = ts_query_pattern_count(entry.query);
  entry.pattern_priority.assign(pattern_count, kDefaultPriority);
  for (uint32_t pat = 0; pat < pattern_count; ++pat) {
    uint32_t step_count = 0;
    const TSQueryPredicateStep* steps = ts_query_predicates_for_pattern(entry.query, pat, &step_count);
    for (uint32_t s = 0; s < step_count; ++s) {
      if (steps[s].type != TSQueryPredicateStepTypeString) { continue; }
      uint32_t name_len = 0;
      const char* pred = ts_query_string_value_for_id(entry.query, steps[s].value_id, &name_len);
      if (std::string_view(pred, name_len) != "set!") { continue; }
      // Next step should be the property name ("priority")
      if (s + 2 < step_count &&
          steps[s + 1].type == TSQueryPredicateStepTypeString &&
          steps[s + 2].type == TSQueryPredicateStepTypeString) {
        uint32_t key_len = 0;
        const char* key = ts_query_string_value_for_id(entry.query, steps[s + 1].value_id, &key_len);
        if (std::string_view(key, key_len) == "priority") {
          uint32_t val_len = 0;
          const char* val = ts_query_string_value_for_id(entry.query, steps[s + 2].value_id, &val_len);
          entry.pattern_priority[pat] = std::atoi(std::string(val, val_len).c_str());
        }
      }
      break;
    }
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
