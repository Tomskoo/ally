#pragma once

#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace ally::opencode {

// --- Health ---

struct HealthResponse {
  bool healthy{};
  std::optional<std::string> version;
};

inline void from_json(const nlohmann::json& json, HealthResponse& val) {
  json.at("healthy").get_to(val.healthy);
  if (json.contains("version") && !json.at("version").is_null()) {
    val.version = json.at("version").get<std::string>();
  }
}

// --- Sessions ---

struct Session {
  std::string id;
  std::optional<std::string> title;
  nlohmann::json extra;
};

inline void from_json(const nlohmann::json& json, Session& val) {
  json.at("id").get_to(val.id);
  if (json.contains("title") && !json.at("title").is_null()) {
    val.title = json.at("title").get<std::string>();
  }
  val.extra = json;
}

inline void to_json(nlohmann::json& json, const Session& val) {
  json = val.extra;
  json["id"] = val.id;
  if (val.title) {
    json["title"] = *val.title;
  }
}

struct CreateSessionRequest {
  std::optional<std::string> title;
  nlohmann::json extra;
};

inline void to_json(nlohmann::json& json, const CreateSessionRequest& val) {
  json = val.extra;
  if (val.title) {
    json["title"] = *val.title;
  }
}

struct UpdateSessionRequest {
  std::optional<std::string> title;
  nlohmann::json extra;
};

inline void to_json(nlohmann::json& json, const UpdateSessionRequest& val) {
  json = val.extra;
  if (val.title) {
    json["title"] = *val.title;
  }
}

struct ForkSessionRequest {
  nlohmann::json extra;
};

inline void to_json(nlohmann::json& json, const ForkSessionRequest& val) { json = val.extra; }

struct RevertRequest {
  nlohmann::json extra;
};

inline void to_json(nlohmann::json& json, const RevertRequest& val) { json = val.extra; }

struct InitSessionRequest {
  nlohmann::json extra;
};

inline void to_json(nlohmann::json& json, const InitSessionRequest& val) { json = val.extra; }

// --- Messages ---

struct MessageInfo {
  std::string id;
  nlohmann::json extra;
};

inline void from_json(const nlohmann::json& json, MessageInfo& val) {
  json.at("id").get_to(val.id);
  val.extra = json;
}

struct MessageWithParts {
  MessageInfo info;
  std::vector<nlohmann::json> parts;
};

inline void from_json(const nlohmann::json& json, MessageWithParts& val) {
  if (json.contains("info") && json.at("info").is_object()) {
    val.info = json.at("info").get<MessageInfo>();
  } else {
    val.info = json.get<MessageInfo>();
  }
  if (json.contains("parts") && json.at("parts").is_array()) {
    val.parts = json.at("parts").get<std::vector<nlohmann::json>>();
  }
}

struct SendMessageRequest {
  std::string content;
  nlohmann::json extra;
};

inline void to_json(nlohmann::json& json, const SendMessageRequest& val) {
  json = val.extra;
  json["content"] = val.content;
}

struct AsyncPromptRequest {
  nlohmann::json data;
};

inline void to_json(nlohmann::json& json, const AsyncPromptRequest& val) { json = val.data; }

struct CommandRequest {
  std::string command;
  std::string arguments;
  nlohmann::json extra;
};

inline void to_json(nlohmann::json& json, const CommandRequest& val) {
  json = val.extra;
  json["command"] = val.command;
  json["arguments"] = val.arguments;
}

struct ShellRequest {
  std::string command;
  nlohmann::json extra;
};

inline void to_json(nlohmann::json& json, const ShellRequest& val) {
  json = val.extra;
  json["command"] = val.command;
}

// --- Configuration ---

struct PatchConfigRequest {
  nlohmann::json data;
};

inline void to_json(nlohmann::json& json, const PatchConfigRequest& val) { json = val.data; }

// --- Models ---

struct ModelInfo {
  std::string id;
  std::string name;
  std::string provider;
};

inline void from_json(const nlohmann::json& json, ModelInfo& val) {
  json.at("id").get_to(val.id);
  json.at("name").get_to(val.name);
  json.at("provider").get_to(val.provider);
}

// --- Providers ---

struct ListProvidersResult {
  std::vector<nlohmann::json> providers;
  std::vector<std::string> connected;
};

// --- MCP ---

struct McpCreateRequest {
  nlohmann::json data;
};

inline void to_json(nlohmann::json& json, const McpCreateRequest& val) { json = val.data; }

// --- TUI ---

struct ToastRequest {
  std::string message;
  nlohmann::json extra;
};

inline void to_json(nlohmann::json& json, const ToastRequest& val) {
  json = val.extra;
  json["message"] = val.message;
}

struct ExecuteCommandRequest {
  std::string command;
  nlohmann::json extra;
};

inline void to_json(nlohmann::json& json, const ExecuteCommandRequest& val) {
  json = val.extra;
  json["command"] = val.command;
}

// --- SSE Events ---

struct OpenCodeEvent {
  std::string event_type;
  nlohmann::json data;
};

// --- Opaque response types ---
// These store the full JSON response for forward compatibility.

struct FileDiff {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, FileDiff& val) { val.data = json; }

struct PermissionResponse {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, PermissionResponse& val) { val.data = json; }

struct Todo {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, Todo& val) { val.data = json; }

struct SessionStatusResponse {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, SessionStatusResponse& val) { val.data = json; }

struct FindMatch {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, FindMatch& val) { val.data = json; }

struct Symbol {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, Symbol& val) { val.data = json; }

struct FileNode {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, FileNode& val) { val.data = json; }

struct FileContentResponse {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, FileContentResponse& val) { val.data = json; }

struct FileStatusResponse {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, FileStatusResponse& val) { val.data = json; }

struct ToolIdsResponse {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, ToolIdsResponse& val) { val.data = json; }

struct ToolListResponse {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, ToolListResponse& val) { val.data = json; }

struct LspStatusResponse {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, LspStatusResponse& val) { val.data = json; }

struct FormatterStatusResponse {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, FormatterStatusResponse& val) { val.data = json; }

struct McpStatusResponse {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, McpStatusResponse& val) { val.data = json; }

struct AgentInfo {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, AgentInfo& val) { val.data = json; }

struct OAuthAuthorization {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, OAuthAuthorization& val) { val.data = json; }

struct ProviderAuthMethod {
  nlohmann::json data;
};

inline void from_json(const nlohmann::json& json, ProviderAuthMethod& val) { val.data = json; }

// ProviderAuthMap: provider_id -> list of auth methods
using ProviderAuthMap = std::unordered_map<std::string, std::vector<ProviderAuthMethod>>;

// --- Questions ---

struct QuestionOption {
  std::string label;
  std::string description;
};

inline void from_json(const nlohmann::json& json, QuestionOption& val) {
  json.at("label").get_to(val.label);
  if (json.contains("description") && json["description"].is_string()) {
    val.description = json["description"].get<std::string>();
  }
}

struct QuestionItem {
  std::string question;
  std::string header;
  std::vector<QuestionOption> options;
  bool multiple = false;
  bool custom = true;
};

inline void from_json(const nlohmann::json& json, QuestionItem& val) {
  json.at("question").get_to(val.question);
  if (json.contains("header") && json["header"].is_string()) {
    val.header = json["header"].get<std::string>();
  }
  if (json.contains("options") && json["options"].is_array()) {
    val.options = json["options"].get<std::vector<QuestionOption>>();
  }
  if (json.contains("multiple") && json["multiple"].is_boolean()) {
    val.multiple = json["multiple"].get<bool>();
  }
  if (json.contains("custom") && json["custom"].is_boolean()) {
    val.custom = json["custom"].get<bool>();
  }
}

struct QuestionRequest {
  std::string id;
  std::string session_id;
  std::vector<QuestionItem> questions;
  nlohmann::json extra;  // full JSON for forward compatibility
};

inline void from_json(const nlohmann::json& json, QuestionRequest& val) {
  json.at("id").get_to(val.id);
  if (json.contains("sessionID") && json["sessionID"].is_string()) {
    val.session_id = json["sessionID"].get<std::string>();
  }
  if (json.contains("questions") && json["questions"].is_array()) {
    val.questions = json["questions"].get<std::vector<QuestionItem>>();
  }
  val.extra = json;
}

struct QuestionReplyRequest {
  std::vector<std::vector<std::string>> answers;
};

inline void to_json(nlohmann::json& json, const QuestionReplyRequest& val) { json = {{"answers", val.answers}}; }

}  // namespace ally::opencode
