#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "httplib.h"
#include "nlohmann/json.hpp"
#include "src/opencode/Error.hpp"
#include "src/opencode/Types.hpp"

namespace ally::opencode {

class OpenCodeClient {
 public:
  static auto Create(std::string base_url) -> OpenCodeClient;

  // Health
  [[nodiscard]] auto Health() -> Result<HealthResponse>;

  // Sessions
  [[nodiscard]] auto ListSessions() -> Result<std::vector<Session>>;
  [[nodiscard]] auto CreateSession(const CreateSessionRequest& req) -> Result<Session>;
  [[nodiscard]] auto GetSession(const std::string& session_id) -> Result<Session>;
  [[nodiscard]] auto UpdateSession(const std::string& session_id, const UpdateSessionRequest& req) -> Result<Session>;
  auto DeleteSession(const std::string& session_id) -> Result<std::monostate>;
  [[nodiscard]] auto ForkSession(const std::string& session_id, const ForkSessionRequest& req) -> Result<Session>;
  auto AbortSession(const std::string& session_id) -> Result<std::monostate>;
  [[nodiscard]] auto ShareSession(const std::string& session_id) -> Result<nlohmann::json>;
  [[nodiscard]] auto SessionDiff(const std::string& session_id) -> Result<std::vector<FileDiff>>;
  [[nodiscard]] auto SummarizeSession(const std::string& session_id) -> Result<nlohmann::json>;
  [[nodiscard]] auto RevertSession(const std::string& session_id, const RevertRequest& req) -> Result<nlohmann::json>;
  [[nodiscard]] auto UnrevertSession(const std::string& session_id) -> Result<nlohmann::json>;
  [[nodiscard]] auto SessionPermissions(const std::string& session_id) -> Result<std::vector<PermissionResponse>>;
  [[nodiscard]] auto InitSession(const std::string& session_id, const InitSessionRequest& req) -> Result<nlohmann::json>;
  [[nodiscard]] auto SessionChildren(const std::string& session_id) -> Result<std::vector<Session>>;
  [[nodiscard]] auto SessionTodo(const std::string& session_id) -> Result<std::vector<Todo>>;
  [[nodiscard]] auto SessionStatus(const std::string& session_id) -> Result<SessionStatusResponse>;

  // Messages
  [[nodiscard]] auto ListMessages(const std::string& session_id) -> Result<std::vector<MessageWithParts>>;
  [[nodiscard]] auto SendMessage(const std::string& session_id, const SendMessageRequest& req) -> Result<MessageWithParts>;
  [[nodiscard]] auto GetMessage(const std::string& session_id, const std::string& message_id) -> Result<MessageWithParts>;
  auto PromptAsync(const std::string& session_id, const AsyncPromptRequest& req) -> Result<std::monostate>;
  [[nodiscard]] auto RunCommand(const std::string& session_id, const CommandRequest& req) -> Result<nlohmann::json>;
  [[nodiscard]] auto RunShell(const std::string& session_id, const ShellRequest& req) -> Result<nlohmann::json>;

  // Configuration
  [[nodiscard]] auto GetConfig() -> Result<nlohmann::json>;
  [[nodiscard]] auto PatchConfig(const PatchConfigRequest& req) -> Result<nlohmann::json>;
  [[nodiscard]] auto ConfigProviders() -> Result<nlohmann::json>;

  // Models
  [[nodiscard]] auto ListModels() -> Result<std::vector<ModelInfo>>;

  // Providers
  [[nodiscard]] auto ListProviders() -> Result<ListProvidersResult>;
  [[nodiscard]] auto ProviderAuth() -> Result<ProviderAuthMap>;
  [[nodiscard]] auto ProviderOAuthAuthorize(const std::string& provider_id) -> Result<OAuthAuthorization>;
  [[nodiscard]] auto ProviderOAuthCallback(const std::string& provider_id) -> Result<bool>;

  // Files
  [[nodiscard]] auto FindContent(const std::string& query) -> Result<std::vector<FindMatch>>;
  [[nodiscard]] auto FindFile(const std::string& query) -> Result<std::vector<FindMatch>>;
  [[nodiscard]] auto FindSymbol(const std::string& query) -> Result<std::vector<Symbol>>;
  [[nodiscard]] auto ListFiles() -> Result<std::vector<FileNode>>;
  [[nodiscard]] auto FileContent(const std::string& path) -> Result<FileContentResponse>;
  [[nodiscard]] auto FileStatus() -> Result<std::vector<FileStatusResponse>>;

  // Tools
  [[nodiscard]] auto ToolIds() -> Result<ToolIdsResponse>;
  [[nodiscard]] auto ToolList() -> Result<ToolListResponse>;

  // LSP / Formatter / MCP
  [[nodiscard]] auto LspStatus() -> Result<LspStatusResponse>;
  [[nodiscard]] auto FormatterStatus() -> Result<FormatterStatusResponse>;
  [[nodiscard]] auto McpStatus() -> Result<McpStatusResponse>;
  [[nodiscard]] auto McpCreate(const McpCreateRequest& req) -> Result<nlohmann::json>;

  // Agents
  [[nodiscard]] auto ListAgents() -> Result<std::vector<AgentInfo>>;

  // TUI
  auto TuiToast(const ToastRequest& req) -> Result<std::monostate>;
  [[nodiscard]] auto TuiExecuteCommand(const ExecuteCommandRequest& req) -> Result<nlohmann::json>;

  // Miscellaneous
  [[nodiscard]] auto ListCommands() -> Result<nlohmann::json>;
  [[nodiscard]] auto ProjectInfo() -> Result<nlohmann::json>;
  [[nodiscard]] auto ProjectPath() -> Result<nlohmann::json>;
  [[nodiscard]] auto VcsInfo() -> Result<nlohmann::json>;
  [[nodiscard]] auto AuthInfo() -> Result<nlohmann::json>;
  [[nodiscard]] auto LogInfo() -> Result<nlohmann::json>;
  auto DisposeInstance() -> Result<std::monostate>;

  [[nodiscard]] auto base_url() const -> const std::string& { return base_url_; }

 private:
  explicit OpenCodeClient(std::string base_url);

  template <typename T>
  auto HandleResponse(const httplib::Result& res) -> Result<T>;
  static auto HandleEmptyResponse(const httplib::Result& res) -> Result<std::monostate>;

  std::unique_ptr<httplib::Client> client_;
  std::string base_url_;
};

}  // namespace ally::opencode
