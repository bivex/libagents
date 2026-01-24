#pragma once

#include "../config.hpp"
#include "../events.hpp"
#include "../provider.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

// Forward declarations for Copilot SDK types
namespace copilot
{
class Client;
class Session;
} // namespace copilot

namespace libagents
{

/// Configuration for Copilot provider
struct CopilotProviderConfig
{
    std::string log_level = "info";
    bool use_stdio = false;  // Use TCP transport (more reliable on Windows)
    std::vector<std::string> cli_args = {"--allow-all-tools", "--allow-all-paths"};
};

/// GitHub Copilot provider implementation
class CopilotProvider : public IProvider
{
  public:
    CopilotProvider();
    ~CopilotProvider() override;

    // IProvider interface
    std::string name() const override { return "copilot"; }
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

    /// Set the maximum time to wait for SessionIdle before timing out
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
    std::string get_session_id() const;

  private:
    /// Ensure session is created
    bool ensure_session();

    bool initialized_ = false;
    std::unique_ptr<copilot::Client> client_;
    std::shared_ptr<copilot::Session> session_; // shared_ptr per SDK API
    std::unordered_map<std::string, std::function<std::string(const std::string&)>>
        current_tool_handlers_;
    std::vector<Tool> registered_tools_; // Store libagents tools for conversion
    std::string system_prompt_;
    CopilotProviderConfig copilot_config_;
    std::string session_id_; // Stored for persistence across process restarts
    BYOKConfig byok_;        // BYOK configuration for custom API keys
    std::chrono::milliseconds response_timeout_{std::chrono::seconds(60)};
};

} // namespace libagents
