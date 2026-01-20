#pragma once

#include "../config.hpp"
#include "../events.hpp"
#include "../provider.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations for Claude SDK types
namespace claude
{
struct ClaudeOptions;
class ClaudeClient;
} // namespace claude

namespace libagents
{

/// Configuration for Claude provider
struct ClaudeProviderConfig
{
    std::string permission_mode = "bypassPermissions";
    bool sanitize_environment = false;
    bool inherit_environment = true;
    std::function<void(const std::string&)> stderr_callback;
};

/// Anthropic Claude provider implementation
class ClaudeProvider : public IProvider
{
  public:
    ClaudeProvider();
    ~ClaudeProvider() override;

    // IProvider interface
    std::string name() const override { return "claude"; }
    bool initialize(const ProviderConfig& config = {}) override;
    void shutdown() override;
    bool is_initialized() const override { return initialized_; }
    std::unique_ptr<ISession> create_session(const SessionConfig& config) override;

    // ─────────────────────────────────────────────────────────────────────────
    // Configuration (call before starting session)
    // ─────────────────────────────────────────────────────────────────────────

    /// Register tools (call once before first query)
    void register_tools(const std::vector<Tool>& tools);

    /// Set system prompt (call once before first query)
    void set_system_prompt(const std::string& prompt);

    /// Set the maximum time to wait for a response before timing out
    void set_response_timeout(std::chrono::milliseconds timeout);

    /// Set session ID to resume (call before first query to resume)
    void set_session_id(const std::string& session_id);

    /// Configure BYOK (Bring Your Own Key) - call before initialize()
    void set_byok(const BYOKConfig& config);

    // ─────────────────────────────────────────────────────────────────────────
    // Query Interface
    // ─────────────────────────────────────────────────────────────────────────

    /// Send a query (session created automatically on first call)
    /// @param query The user's message
    /// @return The AI's response text
    std::string send_query(const std::string& query);

    /// Send a query with event callback for streaming events
    /// @param query The user's message
    /// @param callback Called for tool calls, results, and content
    /// @return The AI's response text
    std::string send_query(const std::string& query, EventCallback callback);

    /// Abort the current query if possible
    void abort();

    /// Clear the current session (next query starts fresh)
    void clear_session();

    /// Get the current session ID for persistence
    std::string get_session_id() const { return session_id_; }

  private:
    /// Ensure client is connected
    bool ensure_connection();

    bool initialized_ = false;
    bool connected_ = false;
    bool primed_ = false;
    std::string session_id_;
    std::string system_prompt_;
    std::unique_ptr<claude::ClaudeOptions> options_;
    std::unique_ptr<claude::ClaudeClient> client_;
    ClaudeProviderConfig claude_config_;
    std::unordered_map<std::string, std::function<std::string(const std::string&)>>
        current_tool_handlers_;
    std::vector<Tool> registered_tools_; // Need full Tool info to create MCP tools on connect
    BYOKConfig byok_;                    // BYOK configuration for custom API keys
    std::chrono::milliseconds response_timeout_{std::chrono::seconds(60)};
};

} // namespace libagents
