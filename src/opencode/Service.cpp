#include "src/opencode/Service.hpp"

#include <shared_mutex>
#include <string>
#include <type_traits>
#include <utility>

#include "src/opencode/Client.hpp"

namespace ally::opencode {

namespace {

template <typename Func>
auto WithClient(OpenCodeState& state, std::shared_mutex& mtx, Func&& func) -> std::invoke_result_t<Func, OpenCodeClient&> {
  std::string url;
  {
    std::shared_lock lock(mtx);
    if (!state.client) {
      return OpenCodeError{OpenCodeErrorKind::NotRunning, "server not running"};
    }
    url = state.client->base_url();
  }
  auto client = OpenCodeClient::Create(std::move(url));
  return std::forward<Func>(func)(client);
}

}  // namespace

// --- Health ---

auto Health(OpenCodeState& state, std::shared_mutex& mtx) -> Result<HealthResponse> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<HealthResponse> { return client.Health(); });
}

// --- Sessions ---

auto ListSessions(OpenCodeState& state, std::shared_mutex& mtx) -> Result<std::vector<Session>> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<std::vector<Session>> { return client.ListSessions(); });
}

auto CreateSession(OpenCodeState& state, std::shared_mutex& mtx, const CreateSessionRequest& req) -> Result<Session> {
  return WithClient(state, mtx, [&req](OpenCodeClient& client) -> Result<Session> { return client.CreateSession(req); });
}

auto GetSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<Session> {
  return WithClient(state, mtx, [&session_id](OpenCodeClient& client) -> Result<Session> { return client.GetSession(session_id); });
}

auto UpdateSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const UpdateSessionRequest& req)
    -> Result<Session> {
  return WithClient(state, mtx, [&session_id, &req](OpenCodeClient& client) -> Result<Session> { return client.UpdateSession(session_id, req); });
}

auto DeleteSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<std::monostate> {
  return WithClient(state, mtx, [&session_id](OpenCodeClient& client) -> Result<std::monostate> { return client.DeleteSession(session_id); });
}

auto ForkSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const ForkSessionRequest& req)
    -> Result<Session> {
  return WithClient(state, mtx, [&session_id, &req](OpenCodeClient& client) -> Result<Session> { return client.ForkSession(session_id, req); });
}

auto AbortSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<std::monostate> {
  return WithClient(state, mtx, [&session_id](OpenCodeClient& client) -> Result<std::monostate> { return client.AbortSession(session_id); });
}

auto ShareSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<nlohmann::json> {
  return WithClient(state, mtx, [&session_id](OpenCodeClient& client) -> Result<nlohmann::json> { return client.ShareSession(session_id); });
}

auto SessionDiff(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<std::vector<FileDiff>> {
  return WithClient(state, mtx, [&session_id](OpenCodeClient& client) -> Result<std::vector<FileDiff>> { return client.SessionDiff(session_id); });
}

auto SummarizeSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<nlohmann::json> {
  return WithClient(state, mtx, [&session_id](OpenCodeClient& client) -> Result<nlohmann::json> { return client.SummarizeSession(session_id); });
}

auto RevertSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const RevertRequest& req)
    -> Result<nlohmann::json> {
  return WithClient(state, mtx, [&session_id, &req](OpenCodeClient& client) -> Result<nlohmann::json> { return client.RevertSession(session_id, req); });
}

auto UnrevertSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<nlohmann::json> {
  return WithClient(state, mtx, [&session_id](OpenCodeClient& client) -> Result<nlohmann::json> { return client.UnrevertSession(session_id); });
}

auto SessionPermissions(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id)
    -> Result<std::vector<PermissionResponse>> {
  return WithClient(state, mtx, [&session_id](OpenCodeClient& client) -> Result<std::vector<PermissionResponse>> { return client.SessionPermissions(session_id); });
}

auto InitSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const InitSessionRequest& req)
    -> Result<nlohmann::json> {
  return WithClient(state, mtx, [&session_id, &req](OpenCodeClient& client) -> Result<nlohmann::json> { return client.InitSession(session_id, req); });
}

auto SessionChildren(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<std::vector<Session>> {
  return WithClient(state, mtx, [&session_id](OpenCodeClient& client) -> Result<std::vector<Session>> { return client.SessionChildren(session_id); });
}

auto SessionTodo(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<std::vector<Todo>> {
  return WithClient(state, mtx, [&session_id](OpenCodeClient& client) -> Result<std::vector<Todo>> { return client.SessionTodo(session_id); });
}

auto SessionStatus(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<SessionStatusResponse> {
  return WithClient(state, mtx, [&session_id](OpenCodeClient& client) -> Result<SessionStatusResponse> { return client.SessionStatus(session_id); });
}

// --- Messages ---

auto ListMessages(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id)
    -> Result<std::vector<MessageWithParts>> {
  return WithClient(state, mtx, [&session_id](OpenCodeClient& client) -> Result<std::vector<MessageWithParts>> { return client.ListMessages(session_id); });
}

auto SendMessage(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const SendMessageRequest& req)
    -> Result<MessageWithParts> {
  return WithClient(state, mtx, [&session_id, &req](OpenCodeClient& client) -> Result<MessageWithParts> { return client.SendMessage(session_id, req); });
}

auto GetMessage(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const std::string& message_id)
    -> Result<MessageWithParts> {
  return WithClient(state, mtx,
                    [&session_id, &message_id](OpenCodeClient& client) -> Result<MessageWithParts> { return client.GetMessage(session_id, message_id); });
}

auto PromptAsync(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const AsyncPromptRequest& req)
    -> Result<std::monostate> {
  return WithClient(state, mtx, [&session_id, &req](OpenCodeClient& client) -> Result<std::monostate> { return client.PromptAsync(session_id, req); });
}

auto RunCommand(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const CommandRequest& req)
    -> Result<nlohmann::json> {
  return WithClient(state, mtx, [&session_id, &req](OpenCodeClient& client) -> Result<nlohmann::json> { return client.RunCommand(session_id, req); });
}

auto RunShell(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const ShellRequest& req)
    -> Result<nlohmann::json> {
  return WithClient(state, mtx, [&session_id, &req](OpenCodeClient& client) -> Result<nlohmann::json> { return client.RunShell(session_id, req); });
}

// --- Configuration ---

auto GetConfig(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<nlohmann::json> { return client.GetConfig(); });
}

auto PatchConfig(OpenCodeState& state, std::shared_mutex& mtx, const PatchConfigRequest& req) -> Result<nlohmann::json> {
  return WithClient(state, mtx, [&req](OpenCodeClient& client) -> Result<nlohmann::json> { return client.PatchConfig(req); });
}

auto ConfigProviders(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<nlohmann::json> { return client.ConfigProviders(); });
}

// --- Models ---

auto ListModels(OpenCodeState& state, std::shared_mutex& mtx) -> Result<std::vector<ModelInfo>> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<std::vector<ModelInfo>> { return client.ListModels(); });
}

// --- Providers ---

auto ListProviders(OpenCodeState& state, std::shared_mutex& mtx) -> Result<ListProvidersResult> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<ListProvidersResult> { return client.ListProviders(); });
}

auto ProviderAuth(OpenCodeState& state, std::shared_mutex& mtx) -> Result<ProviderAuthMap> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<ProviderAuthMap> { return client.ProviderAuth(); });
}

auto ProviderOAuthAuthorize(OpenCodeState& state, std::shared_mutex& mtx, const std::string& provider_id)
    -> Result<OAuthAuthorization> {
  return WithClient(state, mtx, [&provider_id](OpenCodeClient& client) -> Result<OAuthAuthorization> { return client.ProviderOAuthAuthorize(provider_id); });
}

auto ProviderOAuthCallback(OpenCodeState& state, std::shared_mutex& mtx, const std::string& provider_id) -> Result<bool> {
  return WithClient(state, mtx, [&provider_id](OpenCodeClient& client) -> Result<bool> { return client.ProviderOAuthCallback(provider_id); });
}

// --- Files ---

auto FindContent(OpenCodeState& state, std::shared_mutex& mtx, const std::string& query) -> Result<std::vector<FindMatch>> {
  return WithClient(state, mtx, [&query](OpenCodeClient& client) -> Result<std::vector<FindMatch>> { return client.FindContent(query); });
}

auto FindFile(OpenCodeState& state, std::shared_mutex& mtx, const std::string& query) -> Result<std::vector<FindMatch>> {
  return WithClient(state, mtx, [&query](OpenCodeClient& client) -> Result<std::vector<FindMatch>> { return client.FindFile(query); });
}

auto FindSymbol(OpenCodeState& state, std::shared_mutex& mtx, const std::string& query) -> Result<std::vector<Symbol>> {
  return WithClient(state, mtx, [&query](OpenCodeClient& client) -> Result<std::vector<Symbol>> { return client.FindSymbol(query); });
}

auto ListFiles(OpenCodeState& state, std::shared_mutex& mtx) -> Result<std::vector<FileNode>> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<std::vector<FileNode>> { return client.ListFiles(); });
}

auto FileContent(OpenCodeState& state, std::shared_mutex& mtx, const std::string& path) -> Result<FileContentResponse> {
  return WithClient(state, mtx, [&path](OpenCodeClient& client) -> Result<FileContentResponse> { return client.FileContent(path); });
}

auto FileStatus(OpenCodeState& state, std::shared_mutex& mtx) -> Result<std::vector<FileStatusResponse>> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<std::vector<FileStatusResponse>> { return client.FileStatus(); });
}

// --- Tools ---

auto ToolIds(OpenCodeState& state, std::shared_mutex& mtx) -> Result<ToolIdsResponse> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<ToolIdsResponse> { return client.ToolIds(); });
}

auto ToolList(OpenCodeState& state, std::shared_mutex& mtx) -> Result<ToolListResponse> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<ToolListResponse> { return client.ToolList(); });
}

// --- LSP / Formatter / MCP ---

auto LspStatus(OpenCodeState& state, std::shared_mutex& mtx) -> Result<LspStatusResponse> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<LspStatusResponse> { return client.LspStatus(); });
}

auto FormatterStatus(OpenCodeState& state, std::shared_mutex& mtx) -> Result<FormatterStatusResponse> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<FormatterStatusResponse> { return client.FormatterStatus(); });
}

auto McpStatus(OpenCodeState& state, std::shared_mutex& mtx) -> Result<McpStatusResponse> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<McpStatusResponse> { return client.McpStatus(); });
}

auto McpCreate(OpenCodeState& state, std::shared_mutex& mtx, const McpCreateRequest& req) -> Result<nlohmann::json> {
  return WithClient(state, mtx, [&req](OpenCodeClient& client) -> Result<nlohmann::json> { return client.McpCreate(req); });
}

// --- Agents ---

auto ListAgents(OpenCodeState& state, std::shared_mutex& mtx) -> Result<std::vector<AgentInfo>> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<std::vector<AgentInfo>> { return client.ListAgents(); });
}

// --- Questions ---

auto ListQuestions(OpenCodeState& state, std::shared_mutex& mtx) -> Result<std::vector<QuestionRequest>> {
  return WithClient(state, mtx,
                    [](OpenCodeClient& client) -> Result<std::vector<QuestionRequest>> { return client.ListQuestions(); });
}

auto ReplyQuestion(OpenCodeState& state, std::shared_mutex& mtx, const std::string& request_id,
                   const QuestionReplyRequest& req) -> Result<std::monostate> {
  return WithClient(state, mtx, [&request_id, &req](OpenCodeClient& client) -> Result<std::monostate> {
    return client.ReplyQuestion(request_id, req);
  });
}

auto RejectQuestion(OpenCodeState& state, std::shared_mutex& mtx, const std::string& request_id) -> Result<std::monostate> {
  return WithClient(state, mtx,
                    [&request_id](OpenCodeClient& client) -> Result<std::monostate> { return client.RejectQuestion(request_id); });
}

// --- TUI ---

auto TuiToast(OpenCodeState& state, std::shared_mutex& mtx, const ToastRequest& req) -> Result<std::monostate> {
  return WithClient(state, mtx, [&req](OpenCodeClient& client) -> Result<std::monostate> { return client.TuiToast(req); });
}

auto TuiExecuteCommand(OpenCodeState& state, std::shared_mutex& mtx, const ExecuteCommandRequest& req) -> Result<nlohmann::json> {
  return WithClient(state, mtx, [&req](OpenCodeClient& client) -> Result<nlohmann::json> { return client.TuiExecuteCommand(req); });
}

// --- Miscellaneous ---

auto ListCommands(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<nlohmann::json> { return client.ListCommands(); });
}

auto ProjectInfo(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<nlohmann::json> { return client.ProjectInfo(); });
}

auto ProjectPath(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<nlohmann::json> { return client.ProjectPath(); });
}

auto VcsInfo(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<nlohmann::json> { return client.VcsInfo(); });
}

auto AuthInfo(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<nlohmann::json> { return client.AuthInfo(); });
}

auto LogInfo(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<nlohmann::json> { return client.LogInfo(); });
}

auto DisposeInstance(OpenCodeState& state, std::shared_mutex& mtx) -> Result<std::monostate> {
  return WithClient(state, mtx, [](OpenCodeClient& client) -> Result<std::monostate> { return client.DisposeInstance(); });
}

// --- Non-HTTP operations ---

auto GetStatus(OpenCodeState& state, std::shared_mutex& mtx) -> ServerStatusVariant {
  std::shared_lock lock(mtx);
  return state.status;
}

}  // namespace ally::opencode
