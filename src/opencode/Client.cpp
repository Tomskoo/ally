#include "src/opencode/Client.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace ally::opencode {

namespace {

constexpr int kMaxDiagnosticBodyLength = 2000;

auto UrlEncode(const std::string& value) -> std::string {
  std::string result;
  result.reserve(value.size());
  for (char chr : value) {
    if (std::isalnum(static_cast<unsigned char>(chr)) != 0 || chr == '-' || chr == '_' || chr == '.' || chr == '~') {
      result += chr;
    } else {
      result += '%';
      constexpr auto kHex = "0123456789ABCDEF";
      result += kHex[static_cast<unsigned char>(chr) >> 4];    // NOLINT(readability-magic-numbers)
      result += kHex[static_cast<unsigned char>(chr) & 0x0F];  // NOLINT(readability-magic-numbers)
    }
  }
  return result;
}

auto TruncateBody(const std::string& body) -> std::string {
  if (body.size() <= kMaxDiagnosticBodyLength) {
    return body;
  }
  return body.substr(0, kMaxDiagnosticBodyLength) + "...(truncated)";
}

}  // namespace

OpenCodeClient::OpenCodeClient(std::string base_url) : base_url_(std::move(base_url)) {
  client_ = std::make_unique<httplib::Client>(base_url_);
}

auto OpenCodeClient::Create(std::string base_url) -> OpenCodeClient { return OpenCodeClient(std::move(base_url)); }

template <typename T>
auto OpenCodeClient::HandleResponse(const httplib::Result& res) -> Result<T> {
  if (!res) {
    return OpenCodeError{OpenCodeErrorKind::Http, "HTTP transport error: " + httplib::to_string(res.error())};
  }

  if (res->status < 200 || res->status >= 300) {
    return OpenCodeError{OpenCodeErrorKind::ApiError, "API error " + std::to_string(res->status) + ": " + TruncateBody(res->body)};
  }

  auto content_type = res->get_header_value("Content-Type");
  if (content_type.find("json") == std::string::npos && !res->body.empty() && res->body.front() == '<') {
    return OpenCodeError{OpenCodeErrorKind::ApiError, "Server returned HTML instead of JSON"};
  }

  try {
    auto json = nlohmann::json::parse(res->body);
    return json.get<T>();
  } catch (const nlohmann::json::exception& exc) {
    return OpenCodeError{OpenCodeErrorKind::ApiError,
                         "JSON deserialization failed: " + std::string(exc.what()) + " body: " + TruncateBody(res->body)};
  }
}

auto OpenCodeClient::HandleEmptyResponse(const httplib::Result& res) -> Result<std::monostate> {
  if (!res) {
    return OpenCodeError{OpenCodeErrorKind::Http, "HTTP transport error: " + httplib::to_string(res.error())};
  }

  if (res->status < 200 || res->status >= 300) {
    return OpenCodeError{OpenCodeErrorKind::ApiError, "API error " + std::to_string(res->status) + ": " + TruncateBody(res->body)};
  }

  return std::monostate{};
}

// --- Health ---

auto OpenCodeClient::Health() -> Result<HealthResponse> { return HandleResponse<HealthResponse>(client_->Get("/global/health")); }

// --- Sessions ---

auto OpenCodeClient::ListSessions() -> Result<std::vector<Session>> {
  return HandleResponse<std::vector<Session>>(client_->Get("/session"));
}

auto OpenCodeClient::CreateSession(const CreateSessionRequest& req) -> Result<Session> {
  nlohmann::json body = req;
  return HandleResponse<Session>(client_->Post("/session", body.dump(), "application/json"));
}

auto OpenCodeClient::GetSession(const std::string& session_id) -> Result<Session> {
  return HandleResponse<Session>(client_->Get("/session/" + session_id));
}

auto OpenCodeClient::UpdateSession(const std::string& session_id, const UpdateSessionRequest& req) -> Result<Session> {
  nlohmann::json body = req;
  return HandleResponse<Session>(client_->Patch("/session/" + session_id, body.dump(), "application/json"));
}

auto OpenCodeClient::DeleteSession(const std::string& session_id) -> Result<std::monostate> {
  return HandleEmptyResponse(client_->Delete("/session/" + session_id));
}

auto OpenCodeClient::ForkSession(const std::string& session_id, const ForkSessionRequest& req) -> Result<Session> {
  nlohmann::json body = req;
  return HandleResponse<Session>(client_->Post("/session/" + session_id + "/fork", body.dump(), "application/json"));
}

auto OpenCodeClient::AbortSession(const std::string& session_id) -> Result<std::monostate> {
  return HandleEmptyResponse(client_->Post("/session/" + session_id + "/abort", "", "application/json"));
}

auto OpenCodeClient::ShareSession(const std::string& session_id) -> Result<nlohmann::json> {
  return HandleResponse<nlohmann::json>(client_->Post("/session/" + session_id + "/share", "", "application/json"));
}

auto OpenCodeClient::SessionDiff(const std::string& session_id) -> Result<std::vector<FileDiff>> {
  return HandleResponse<std::vector<FileDiff>>(client_->Get("/session/" + session_id + "/diff"));
}

auto OpenCodeClient::SummarizeSession(const std::string& session_id) -> Result<nlohmann::json> {
  return HandleResponse<nlohmann::json>(client_->Post("/session/" + session_id + "/summarize", "", "application/json"));
}

auto OpenCodeClient::RevertSession(const std::string& session_id, const RevertRequest& req) -> Result<nlohmann::json> {
  nlohmann::json body = req;
  return HandleResponse<nlohmann::json>(client_->Post("/session/" + session_id + "/revert", body.dump(), "application/json"));
}

auto OpenCodeClient::UnrevertSession(const std::string& session_id) -> Result<nlohmann::json> {
  return HandleResponse<nlohmann::json>(client_->Post("/session/" + session_id + "/unrevert", "", "application/json"));
}

auto OpenCodeClient::SessionPermissions(const std::string& session_id) -> Result<std::vector<PermissionResponse>> {
  return HandleResponse<std::vector<PermissionResponse>>(client_->Get("/session/" + session_id + "/permissions"));
}

auto OpenCodeClient::InitSession(const std::string& session_id, const InitSessionRequest& req) -> Result<nlohmann::json> {
  nlohmann::json body = req;
  return HandleResponse<nlohmann::json>(client_->Post("/session/" + session_id + "/init", body.dump(), "application/json"));
}

auto OpenCodeClient::SessionChildren(const std::string& session_id) -> Result<std::vector<Session>> {
  return HandleResponse<std::vector<Session>>(client_->Get("/session/" + session_id + "/children"));
}

auto OpenCodeClient::SessionTodo(const std::string& session_id) -> Result<std::vector<Todo>> {
  return HandleResponse<std::vector<Todo>>(client_->Get("/session/" + session_id + "/todo"));
}

auto OpenCodeClient::SessionStatus(const std::string& session_id) -> Result<SessionStatusResponse> {
  return HandleResponse<SessionStatusResponse>(client_->Get("/session/" + session_id + "/status"));
}

// --- Messages ---

auto OpenCodeClient::ListMessages(const std::string& session_id) -> Result<std::vector<MessageWithParts>> {
  return HandleResponse<std::vector<MessageWithParts>>(client_->Get("/session/" + session_id + "/message"));
}

auto OpenCodeClient::SendMessage(const std::string& session_id, const SendMessageRequest& req) -> Result<MessageWithParts> {
  nlohmann::json body = req;
  return HandleResponse<MessageWithParts>(client_->Post("/session/" + session_id + "/message", body.dump(), "application/json"));
}

auto OpenCodeClient::GetMessage(const std::string& session_id, const std::string& message_id) -> Result<MessageWithParts> {
  return HandleResponse<MessageWithParts>(client_->Get("/session/" + session_id + "/message/" + message_id));
}

auto OpenCodeClient::PromptAsync(const std::string& session_id, const AsyncPromptRequest& req) -> Result<std::monostate> {
  nlohmann::json body = req;
  auto res = client_->Post("/session/" + session_id + "/prompt_async", body.dump(), "application/json");
  if (!res) {
    return OpenCodeError{OpenCodeErrorKind::Http, "HTTP transport error: " + httplib::to_string(res.error())};
  }
  if (res->status < 200 || res->status >= 300) {
    return OpenCodeError{OpenCodeErrorKind::ApiError, "API error " + std::to_string(res->status) + ": " + TruncateBody(res->body)};
  }
  return std::monostate{};
}

auto OpenCodeClient::RunCommand(const std::string& session_id, const CommandRequest& req) -> Result<nlohmann::json> {
  nlohmann::json body = req;
  return HandleResponse<nlohmann::json>(client_->Post("/session/" + session_id + "/command", body.dump(), "application/json"));
}

auto OpenCodeClient::RunShell(const std::string& session_id, const ShellRequest& req) -> Result<nlohmann::json> {
  nlohmann::json body = req;
  return HandleResponse<nlohmann::json>(client_->Post("/session/" + session_id + "/shell", body.dump(), "application/json"));
}

// --- Configuration ---

auto OpenCodeClient::GetConfig() -> Result<nlohmann::json> { return HandleResponse<nlohmann::json>(client_->Get("/config")); }

auto OpenCodeClient::PatchConfig(const PatchConfigRequest& req) -> Result<nlohmann::json> {
  nlohmann::json body = req;
  return HandleResponse<nlohmann::json>(client_->Patch("/config", body.dump(), "application/json"));
}

auto OpenCodeClient::ConfigProviders() -> Result<nlohmann::json> {
  return HandleResponse<nlohmann::json>(client_->Get("/config/providers"));
}

// --- Models ---

auto OpenCodeClient::ListModels() -> Result<std::vector<ModelInfo>> {
  auto providers_result = ConfigProviders();
  if (!is_ok(providers_result)) {
    return get_error(providers_result);
  }

  const auto& data = get_value(providers_result);
  std::vector<ModelInfo> models;

  // Walk "default" key: { provider_id: model_id }
  if (data.contains("default") && data.at("default").is_object()) {
    for (const auto& [provider_id, model_id] : data.at("default").items()) {
      if (model_id.is_string()) {
        auto mid = model_id.get<std::string>();
        models.push_back(ModelInfo{.id = mid, .name = mid, .provider = provider_id});
      }
    }
  }

  // Walk "providers" key: array of provider objects
  if (data.contains("providers") && data.at("providers").is_array()) {
    for (const auto& prov : data.at("providers")) {
      std::string provider_id;
      if (prov.contains("id") && prov.at("id").is_string()) {
        provider_id = prov.at("id").get<std::string>();
      }

      if (!prov.contains("models")) {
        continue;
      }

      const auto& prov_models = prov.at("models");
      auto add_if_new = [&](const std::string& mid) -> void {
        bool exists = std::any_of(models.begin(), models.end(), [&mid](const ModelInfo& model) -> bool { return model.id == mid; });
        if (!exists) {
          models.push_back(ModelInfo{.id = mid, .name = mid, .provider = provider_id});
        }
      };

      if (prov_models.is_array()) {
        for (const auto& model_entry : prov_models) {
          if (model_entry.is_string()) {
            add_if_new(model_entry.get<std::string>());
          }
        }
      } else if (prov_models.is_object()) {
        for (const auto& [mid, _val] : prov_models.items()) {
          add_if_new(mid);
        }
      }
    }
  }

  return models;
}

// --- Providers ---

auto OpenCodeClient::ListProviders() -> Result<ListProvidersResult> {
  auto res = client_->Get("/provider");
  if (!res) {
    return OpenCodeError{OpenCodeErrorKind::Http, "HTTP transport error: " + httplib::to_string(res.error())};
  }
  if (res->status < 200 || res->status >= 300) {
    return OpenCodeError{OpenCodeErrorKind::ApiError, "API error " + std::to_string(res->status) + ": " + TruncateBody(res->body)};
  }

  try {
    auto json = nlohmann::json::parse(res->body);
    ListProvidersResult result;
    if (json.contains("all") && json.at("all").is_array()) {
      result.providers = json.at("all").get<std::vector<nlohmann::json>>();
    }
    if (json.contains("connected") && json.at("connected").is_array()) {
      result.connected = json.at("connected").get<std::vector<std::string>>();
    }
    return result;
  } catch (const nlohmann::json::exception& exc) {
    return OpenCodeError{OpenCodeErrorKind::ApiError,
                         "JSON deserialization failed: " + std::string(exc.what()) + " body: " + TruncateBody(res->body)};
  }
}

auto OpenCodeClient::ProviderAuth() -> Result<ProviderAuthMap> {
  return HandleResponse<ProviderAuthMap>(client_->Get("/provider/auth"));
}

auto OpenCodeClient::ProviderOAuthAuthorize(const std::string& provider_id) -> Result<OAuthAuthorization> {
  return HandleResponse<OAuthAuthorization>(client_->Post("/provider/" + provider_id + "/oauth/authorize", "", "application/json"));
}

auto OpenCodeClient::ProviderOAuthCallback(const std::string& provider_id) -> Result<bool> {
  return HandleResponse<bool>(client_->Post("/provider/" + provider_id + "/oauth/callback", "", "application/json"));
}

// --- Files ---

auto OpenCodeClient::FindContent(const std::string& query) -> Result<std::vector<FindMatch>> {
  return HandleResponse<std::vector<FindMatch>>(client_->Get("/file/find?q=" + UrlEncode(query)));
}

auto OpenCodeClient::FindFile(const std::string& query) -> Result<std::vector<FindMatch>> {
  return HandleResponse<std::vector<FindMatch>>(client_->Get("/file/find_file?q=" + UrlEncode(query)));
}

auto OpenCodeClient::FindSymbol(const std::string& query) -> Result<std::vector<Symbol>> {
  return HandleResponse<std::vector<Symbol>>(client_->Get("/file/find_symbol?q=" + UrlEncode(query)));
}

auto OpenCodeClient::ListFiles() -> Result<std::vector<FileNode>> {
  return HandleResponse<std::vector<FileNode>>(client_->Get("/file"));
}

auto OpenCodeClient::FileContent(const std::string& path) -> Result<FileContentResponse> {
  return HandleResponse<FileContentResponse>(client_->Get("/file/content?path=" + UrlEncode(path)));
}

auto OpenCodeClient::FileStatus() -> Result<std::vector<FileStatusResponse>> {
  return HandleResponse<std::vector<FileStatusResponse>>(client_->Get("/file/status"));
}

// --- Tools ---

auto OpenCodeClient::ToolIds() -> Result<ToolIdsResponse> { return HandleResponse<ToolIdsResponse>(client_->Get("/tool")); }

auto OpenCodeClient::ToolList() -> Result<ToolListResponse> { return HandleResponse<ToolListResponse>(client_->Get("/tool/list")); }

// --- LSP / Formatter / MCP ---

auto OpenCodeClient::LspStatus() -> Result<LspStatusResponse> { return HandleResponse<LspStatusResponse>(client_->Get("/lsp")); }

auto OpenCodeClient::FormatterStatus() -> Result<FormatterStatusResponse> {
  return HandleResponse<FormatterStatusResponse>(client_->Get("/formatter"));
}

auto OpenCodeClient::McpStatus() -> Result<McpStatusResponse> { return HandleResponse<McpStatusResponse>(client_->Get("/mcp")); }

auto OpenCodeClient::McpCreate(const McpCreateRequest& req) -> Result<nlohmann::json> {
  nlohmann::json body = req;
  return HandleResponse<nlohmann::json>(client_->Post("/mcp", body.dump(), "application/json"));
}

// --- Agents ---

auto OpenCodeClient::ListAgents() -> Result<std::vector<AgentInfo>> {
  return HandleResponse<std::vector<AgentInfo>>(client_->Get("/agent"));
}

// --- Questions ---

auto OpenCodeClient::ListQuestions() -> Result<std::vector<QuestionRequest>> {
  return HandleResponse<std::vector<QuestionRequest>>(client_->Get("/question"));
}

auto OpenCodeClient::ReplyQuestion(const std::string& request_id, const QuestionReplyRequest& req) -> Result<std::monostate> {
  nlohmann::json body = req;
  return HandleEmptyResponse(client_->Post("/question/" + request_id + "/reply", body.dump(), "application/json"));
}

auto OpenCodeClient::RejectQuestion(const std::string& request_id) -> Result<std::monostate> {
  return HandleEmptyResponse(client_->Post("/question/" + request_id + "/reject", "{}", "application/json"));
}

// --- TUI ---

auto OpenCodeClient::TuiToast(const ToastRequest& req) -> Result<std::monostate> {
  nlohmann::json body = req;
  return HandleEmptyResponse(client_->Post("/tui/toast", body.dump(), "application/json"));
}

auto OpenCodeClient::TuiExecuteCommand(const ExecuteCommandRequest& req) -> Result<nlohmann::json> {
  nlohmann::json body = req;
  return HandleResponse<nlohmann::json>(client_->Post("/tui/command", body.dump(), "application/json"));
}

// --- Miscellaneous ---

auto OpenCodeClient::ListCommands() -> Result<nlohmann::json> { return HandleResponse<nlohmann::json>(client_->Get("/command")); }

auto OpenCodeClient::ProjectInfo() -> Result<nlohmann::json> { return HandleResponse<nlohmann::json>(client_->Get("/project")); }

auto OpenCodeClient::ProjectPath() -> Result<nlohmann::json> {
  return HandleResponse<nlohmann::json>(client_->Get("/project/path"));
}

auto OpenCodeClient::VcsInfo() -> Result<nlohmann::json> { return HandleResponse<nlohmann::json>(client_->Get("/vcs")); }

auto OpenCodeClient::AuthInfo() -> Result<nlohmann::json> { return HandleResponse<nlohmann::json>(client_->Get("/auth")); }

auto OpenCodeClient::LogInfo() -> Result<nlohmann::json> { return HandleResponse<nlohmann::json>(client_->Get("/log")); }

auto OpenCodeClient::DisposeInstance() -> Result<std::monostate> {
  return HandleEmptyResponse(client_->Post("/instance/dispose", "", "application/json"));
}

// Explicit template instantiations for HandleResponse
template auto OpenCodeClient::HandleResponse<HealthResponse>(const httplib::Result&) -> Result<HealthResponse>;
template auto OpenCodeClient::HandleResponse<std::vector<Session>>(const httplib::Result&) -> Result<std::vector<Session>>;
template auto OpenCodeClient::HandleResponse<Session>(const httplib::Result&) -> Result<Session>;
template auto OpenCodeClient::HandleResponse<nlohmann::json>(const httplib::Result&) -> Result<nlohmann::json>;
template auto OpenCodeClient::HandleResponse<std::vector<FileDiff>>(const httplib::Result&) -> Result<std::vector<FileDiff>>;
template auto OpenCodeClient::HandleResponse<std::vector<PermissionResponse>>(const httplib::Result&)
    -> Result<std::vector<PermissionResponse>>;
template auto OpenCodeClient::HandleResponse<std::vector<Todo>>(const httplib::Result&) -> Result<std::vector<Todo>>;
template auto OpenCodeClient::HandleResponse<SessionStatusResponse>(const httplib::Result&) -> Result<SessionStatusResponse>;
template auto OpenCodeClient::HandleResponse<std::vector<MessageWithParts>>(const httplib::Result&)
    -> Result<std::vector<MessageWithParts>>;
template auto OpenCodeClient::HandleResponse<MessageWithParts>(const httplib::Result&) -> Result<MessageWithParts>;
template auto OpenCodeClient::HandleResponse<ProviderAuthMap>(const httplib::Result&) -> Result<ProviderAuthMap>;
template auto OpenCodeClient::HandleResponse<OAuthAuthorization>(const httplib::Result&) -> Result<OAuthAuthorization>;
template auto OpenCodeClient::HandleResponse<bool>(const httplib::Result&) -> Result<bool>;
template auto OpenCodeClient::HandleResponse<std::vector<FindMatch>>(const httplib::Result&) -> Result<std::vector<FindMatch>>;
template auto OpenCodeClient::HandleResponse<std::vector<Symbol>>(const httplib::Result&) -> Result<std::vector<Symbol>>;
template auto OpenCodeClient::HandleResponse<std::vector<FileNode>>(const httplib::Result&) -> Result<std::vector<FileNode>>;
template auto OpenCodeClient::HandleResponse<FileContentResponse>(const httplib::Result&) -> Result<FileContentResponse>;
template auto OpenCodeClient::HandleResponse<std::vector<FileStatusResponse>>(const httplib::Result&)
    -> Result<std::vector<FileStatusResponse>>;
template auto OpenCodeClient::HandleResponse<ToolIdsResponse>(const httplib::Result&) -> Result<ToolIdsResponse>;
template auto OpenCodeClient::HandleResponse<ToolListResponse>(const httplib::Result&) -> Result<ToolListResponse>;
template auto OpenCodeClient::HandleResponse<LspStatusResponse>(const httplib::Result&) -> Result<LspStatusResponse>;
template auto OpenCodeClient::HandleResponse<FormatterStatusResponse>(const httplib::Result&) -> Result<FormatterStatusResponse>;
template auto OpenCodeClient::HandleResponse<McpStatusResponse>(const httplib::Result&) -> Result<McpStatusResponse>;
template auto OpenCodeClient::HandleResponse<std::vector<AgentInfo>>(const httplib::Result&) -> Result<std::vector<AgentInfo>>;

}  // namespace ally::opencode
