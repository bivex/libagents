#pragma once

/// @file agent.hpp
/// @brief High-level agent interface for AI-powered applications
///
/// This is the primary interface for building AI agents. It abstracts away
/// provider-specific details and provides a unified way to:
/// - Register tools
/// - Set system prompts
/// - Send queries and receive responses
///
/// Example usage:
/// @code
/// #include <libagents/agent.hpp>
///
/// // Create agent with preferred provider
/// auto agent = libagents::create_agent(libagents::ProviderType::Copilot);
///
/// // Register a calculator tool
/// agent->register_tool({
///     .name = "calculate",
///     .description = "Evaluate a mathematical expression",
///     .parameters_schema = R"({
///         "type": "object",
///         "properties": {
///             "expression": {"type": "string", "description": "Math expression to evaluate"}
///         },
///         "required": ["expression"]
///     })",
///     .handler = [](const std::string& args) {
///         auto j = libagents::json::parse(args);
///         auto expr = j["expression"].get<std::string>();
///         double result = evaluate_expression(expr);  // your math parser
///         return std::to_string(result);
///     }
/// });
///
/// // Set system prompt
/// agent->set_system_prompt("You are a helpful calculator assistant.");
///
/// // Initialize the agent
/// agent->initialize();
///
/// // Send query - agent calls the calculator tool automatically
/// auto response = agent->query("What is 2 + 2 * 3?");
/// // Agent calls calculate({"expression": "2 + 2 * 3"}) -> "8"
/// // response: "The result of 2 + 2 * 3 is 8."
/// @endcode

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config.hpp"
#include "events.hpp"
#include "provider.hpp"

namespace libagents
{

/// Host context for hosted queries (output + cancel)
struct HostContext
{
    EventCallback on_event;             // Optional streaming/event output
    std::function<bool()> should_abort; // Optional cancel predicate
};

/// High-level agent interface - provider agnostic
class IAgent
{
  public:
    virtual ~IAgent() = default;

    // ─────────────────────────────────────────────────────────────────────────
    // Configuration (before initialize)
    // ─────────────────────────────────────────────────────────────────────────

    /// Register a tool that the AI can call
    /// @param tool Tool definition including handler
    virtual void register_tool(const Tool& tool) = 0;

    /// Set the system prompt for the AI
    /// @param prompt System prompt text
    virtual void set_system_prompt(const std::string& prompt) = 0;

    /// Set the model to use (empty = provider default)
    /// @param model Model identifier (e.g., "gpt-4", "claude-sonnet-4-5-20250514")
    virtual void set_model(const std::string& model) = 0;

    /// Set provider-specific option
    /// @param key Option name
    /// @param value Option value
    virtual void set_option(const std::string& key, const std::string& value) = 0;

    /// Configure BYOK (Bring Your Own Key) - call before initialize()
    /// This bypasses the default provider authentication and uses custom API keys
    /// @param config BYOK configuration with API key, base URL, model, etc.
    virtual void set_byok(const BYOKConfig& config) = 0;

    /// Set the maximum time to wait for a response before timing out
    /// @param timeout Timeout duration (default is typically 60 seconds)
    virtual void set_response_timeout(std::chrono::milliseconds timeout) = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Lifecycle
    // ─────────────────────────────────────────────────────────────────────────

    /// Initialize the agent (connects to provider)
    /// @return true if successful
    virtual bool initialize() = 0;

    /// Shutdown the agent
    virtual void shutdown() = 0;

    /// Check if initialized
    virtual bool is_initialized() const = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Query Interface
    // ─────────────────────────────────────────────────────────────────────────

    /// Send a query and get response (blocking)
    /// Tools are called automatically during processing
    /// @param message User's message/question
    /// @return AI's response text
    virtual std::string query(const std::string& message) = 0;

    /// Send a query with streaming callback
    /// @param message User's message/question
    /// @param callback Called for each event (content deltas, tool calls, etc.)
    /// @return Final response text
    virtual std::string query_streaming(const std::string& message, EventCallback callback) = 0;

    /// Send a query with libagents-hosted tooling dispatch
    /// Tools execute on the caller thread; events delivered via HostContext
    virtual std::string query_hosted(const std::string& message, const HostContext& host) = 0;

    /// Abort current query
    virtual void abort() = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Session Management
    // ─────────────────────────────────────────────────────────────────────────

    /// Clear conversation history (start fresh)
    virtual void clear_session() = 0;

    /// Get current session ID (for persistence)
    virtual std::string get_session_id() const = 0;

    /// Resume a previous session
    /// @param session_id Session ID from get_session_id()
    virtual void set_session_id(const std::string& session_id) = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Info
    // ─────────────────────────────────────────────────────────────────────────

    /// Get provider name (e.g., "claude", "copilot")
    virtual std::string provider_name() const = 0;

    /// Check if busy processing a query
    virtual bool is_busy() const = 0;
};

/// Create an agent with the specified provider
/// @param type Provider type (Claude, Copilot)
/// @return Agent instance
std::unique_ptr<IAgent> create_agent(ProviderType type);

} // namespace libagents
