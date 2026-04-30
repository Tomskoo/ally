#pragma once

#include <shared_mutex>
#include <string>
#include <variant>
#include <vector>

#include "nlohmann/json.hpp"
#include "src/opencode/Error.hpp"
#include "src/opencode/State.hpp"
#include "src/opencode/Types.hpp"

namespace ally::opencode {

// Health
auto Health(OpenCodeState& state, std::shared_mutex& mtx) -> Result<HealthResponse>;

// Sessions
auto ListSessions(OpenCodeState& state, std::shared_mutex& mtx) -> Result<std::vector<Session>>;
auto CreateSession(OpenCodeState& state, std::shared_mutex& mtx, const CreateSessionRequest& req) -> Result<Session>;
auto GetSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<Session>;
auto UpdateSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const UpdateSessionRequest& req)
    -> Result<Session>;
auto DeleteSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<std::monostate>;
auto ForkSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const ForkSessionRequest& req)
    -> Result<Session>;
auto AbortSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<std::monostate>;
auto ShareSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<nlohmann::json>;
auto SessionDiff(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<std::vector<FileDiff>>;
auto SummarizeSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<nlohmann::json>;
auto RevertSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const RevertRequest& req)
    -> Result<nlohmann::json>;
auto UnrevertSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<nlohmann::json>;
auto SessionPermissions(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id)
    -> Result<std::vector<PermissionResponse>>;
auto InitSession(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const InitSessionRequest& req)
    -> Result<nlohmann::json>;
auto SessionChildren(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<std::vector<Session>>;
auto SessionTodo(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<std::vector<Todo>>;
auto SessionStatus(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id) -> Result<SessionStatusResponse>;

// Messages
auto ListMessages(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id)
    -> Result<std::vector<MessageWithParts>>;
auto SendMessage(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const SendMessageRequest& req)
    -> Result<MessageWithParts>;
auto GetMessage(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const std::string& message_id)
    -> Result<MessageWithParts>;
auto PromptAsync(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const AsyncPromptRequest& req)
    -> Result<std::monostate>;
auto RunCommand(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const CommandRequest& req)
    -> Result<nlohmann::json>;
auto RunShell(OpenCodeState& state, std::shared_mutex& mtx, const std::string& session_id, const ShellRequest& req)
    -> Result<nlohmann::json>;

// Configuration
auto GetConfig(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json>;
auto PatchConfig(OpenCodeState& state, std::shared_mutex& mtx, const PatchConfigRequest& req) -> Result<nlohmann::json>;
auto ConfigProviders(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json>;

// Models
auto ListModels(OpenCodeState& state, std::shared_mutex& mtx) -> Result<std::vector<ModelInfo>>;

// Providers
auto ListProviders(OpenCodeState& state, std::shared_mutex& mtx) -> Result<ListProvidersResult>;
auto ProviderAuth(OpenCodeState& state, std::shared_mutex& mtx) -> Result<ProviderAuthMap>;
auto ProviderOAuthAuthorize(OpenCodeState& state, std::shared_mutex& mtx, const std::string& provider_id)
    -> Result<OAuthAuthorization>;
auto ProviderOAuthCallback(OpenCodeState& state, std::shared_mutex& mtx, const std::string& provider_id) -> Result<bool>;

// Files
auto FindContent(OpenCodeState& state, std::shared_mutex& mtx, const std::string& query) -> Result<std::vector<FindMatch>>;
auto FindFile(OpenCodeState& state, std::shared_mutex& mtx, const std::string& query) -> Result<std::vector<FindMatch>>;
auto FindSymbol(OpenCodeState& state, std::shared_mutex& mtx, const std::string& query) -> Result<std::vector<Symbol>>;
auto ListFiles(OpenCodeState& state, std::shared_mutex& mtx) -> Result<std::vector<FileNode>>;
auto FileContent(OpenCodeState& state, std::shared_mutex& mtx, const std::string& path) -> Result<FileContentResponse>;
auto FileStatus(OpenCodeState& state, std::shared_mutex& mtx) -> Result<std::vector<FileStatusResponse>>;

// Tools
auto ToolIds(OpenCodeState& state, std::shared_mutex& mtx) -> Result<ToolIdsResponse>;
auto ToolList(OpenCodeState& state, std::shared_mutex& mtx) -> Result<ToolListResponse>;

// LSP / Formatter / MCP
auto LspStatus(OpenCodeState& state, std::shared_mutex& mtx) -> Result<LspStatusResponse>;
auto FormatterStatus(OpenCodeState& state, std::shared_mutex& mtx) -> Result<FormatterStatusResponse>;
auto McpStatus(OpenCodeState& state, std::shared_mutex& mtx) -> Result<McpStatusResponse>;
auto McpCreate(OpenCodeState& state, std::shared_mutex& mtx, const McpCreateRequest& req) -> Result<nlohmann::json>;

// Agents
auto ListAgents(OpenCodeState& state, std::shared_mutex& mtx) -> Result<std::vector<AgentInfo>>;

// Questions
auto ListQuestions(OpenCodeState& state, std::shared_mutex& mtx) -> Result<std::vector<QuestionRequest>>;
auto ReplyQuestion(OpenCodeState& state, std::shared_mutex& mtx, const std::string& request_id,
                   const QuestionReplyRequest& req) -> Result<std::monostate>;
auto RejectQuestion(OpenCodeState& state, std::shared_mutex& mtx, const std::string& request_id) -> Result<std::monostate>;

// TUI
auto TuiToast(OpenCodeState& state, std::shared_mutex& mtx, const ToastRequest& req) -> Result<std::monostate>;
auto TuiExecuteCommand(OpenCodeState& state, std::shared_mutex& mtx, const ExecuteCommandRequest& req) -> Result<nlohmann::json>;

// Miscellaneous
auto ListCommands(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json>;
auto ProjectInfo(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json>;
auto ProjectPath(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json>;
auto VcsInfo(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json>;
auto AuthInfo(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json>;
auto LogInfo(OpenCodeState& state, std::shared_mutex& mtx) -> Result<nlohmann::json>;
auto DisposeInstance(OpenCodeState& state, std::shared_mutex& mtx) -> Result<std::monostate>;

// Non-HTTP operations
auto GetStatus(OpenCodeState& state, std::shared_mutex& mtx) -> ServerStatusVariant;

}  // namespace ally::opencode
