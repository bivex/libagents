#include <chrono>
#include <claude/claude.hpp>
#include <cstdlib>
#include <libagents/providers/claude_provider.hpp>

#ifdef _WIN32
#include <direct.h>
#define SET_ENV(name, value) _putenv_s(name, value)
#else
#define SET_ENV(name, value) setenv(name, value, 1)
#endif

namespace libagents
{

namespace
{
/// Convert AssistantMessageError enum to human-readable string
std::string assistant_error_to_string(claude::AssistantMessageError error)
{
    switch (error)
    {
    case claude::AssistantMessageError::AuthenticationFailed:
        return "Authentication failed - check your API key or login status";
    case claude::AssistantMessageError::BillingError:
        return "Billing error - check your account status";
    case claude::AssistantMessageError::RateLimit:
        return "Rate limit exceeded - please wait and try again";
    case claude::AssistantMessageError::InvalidRequest:
        return "Invalid request";
    case claude::AssistantMessageError::ServerError:
        return "Server error - Claude API is experiencing issues";
    case claude::AssistantMessageError::Unknown:
    default:
        return "Unknown error";
    }
}
} // namespace

ClaudeProvider::ClaudeProvider()
{
    options_ = std::make_unique<claude::ClaudeOptions>();
}

ClaudeProvider::~ClaudeProvider()
{
    shutdown();
}

bool ClaudeProvider::initialize(const ProviderConfig& config)
{
    (void)config; // Not used yet, claude_config_ used instead

    if (initialized_)
        return true;

    // Configure Claude options
    options_->permission_mode = claude_config_.permission_mode;
    options_->sanitize_environment = claude_config_.sanitize_environment;
    options_->inherit_environment = claude_config_.inherit_environment;

    if (claude_config_.stderr_callback)
    {
        options_->stderr_callback = claude_config_.stderr_callback;
    }

    initialized_ = true;
    return true;
}

void ClaudeProvider::shutdown()
{
    if (client_ && connected_)
    {
        try
        {
            client_->disconnect();
        }
        catch (...)
        {
        }
        client_.reset();
        connected_ = false;
    }
    initialized_ = false;
}

void ClaudeProvider::clear_session()
{
    // Disconnect to start fresh session
    if (client_ && connected_)
    {
        try
        {
            client_->disconnect();
        }
        catch (...)
        {
        }
        connected_ = false;
    }
    session_id_.clear();
    primed_ = false;
}

std::unique_ptr<ISession> ClaudeProvider::create_session(const SessionConfig& config)
{
    // TODO: Implement proper ISession wrapper
    // For now, use send_query() as the main interface
    (void)config;
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────────────────────

void ClaudeProvider::register_tools(const std::vector<Tool>& tools)
{
    // Store tool handlers for callback access
    current_tool_handlers_.clear();
    registered_tools_ = tools;

    for (const auto& tool : tools)
    {
        if (tool.handler)
        {
            current_tool_handlers_[tool.name] = tool.handler;
        }
    }

    // If connected, need to reconnect to use new tools
    if (client_ && connected_)
    {
        try
        {
            client_->disconnect();
        }
        catch (...)
        {
        }
        connected_ = false;
    }
}

void ClaudeProvider::set_system_prompt(const std::string& prompt)
{
    system_prompt_ = prompt;
    // If prompt has newlines, default to priming to avoid CLI arg parsing issues.
    bool has_newline = (prompt.find('\n') != std::string::npos) ||
                       (prompt.find('\r') != std::string::npos);
    primed_ = !has_newline;
}

void ClaudeProvider::set_response_timeout(std::chrono::milliseconds timeout)
{
    response_timeout_ = timeout;
}

void ClaudeProvider::set_session_id(const std::string& session_id)
{
    session_id_ = session_id;
    // If resuming, assume primed; if cleared, allow priming again.
    primed_ = !session_id_.empty();
}

void ClaudeProvider::set_byok(const BYOKConfig& config)
{
    byok_ = config;
}

// ─────────────────────────────────────────────────────────────────────────────
// Query Interface
// ─────────────────────────────────────────────────────────────────────────────

bool ClaudeProvider::ensure_connection()
{
    if (connected_)
        return true;

    if (!initialized_)
        return false;

    // Create MCP tools from registered tools
    std::vector<claude::mcp::Tool> mcp_tools;
    std::vector<std::string> allowed_tool_names;

    for (const auto& tool : registered_tools_)
    {
        // Extract first property name from schema (e.g., "command" from dbg_exec)
        std::string param_name = "args";  // Default fallback
        if (!tool.parameters_schema.empty())
        {
            try
            {
                auto schema = json::parse(tool.parameters_schema);
                if (schema.contains("properties") && schema["properties"].is_object())
                {
                    for (auto& [key, val] : schema["properties"].items())
                    {
                        param_name = key;  // Use first property name
                        break;
                    }
                }
            }
            catch (...)
            {
            }
        }

        // Create MCP tool with the correct parameter name
        // Handler receives the VALUE, so we reconstruct JSON for the original handler
        auto mcp_tool = claude::mcp::make_tool(
            tool.name, tool.description,
            [this, name = tool.name, param_name](std::string value) -> json
            {
                auto it = current_tool_handlers_.find(name);
                if (it != current_tool_handlers_.end())
                {
                    // Reconstruct JSON args for original handler
                    json args;
                    args[param_name] = value;
                    auto output = it->second(args.dump());
                    json result;
                    result["output"] = output;
                    result["success"] = true;
                    return result;
                }
                json result;
                result["output"] = "Error: Tool handler not found";
                result["success"] = false;
                return result;
            },
            std::vector<std::string>{param_name});

        mcp_tools.push_back(std::move(mcp_tool));
        allowed_tool_names.push_back("mcp__tools__" + tool.name);
    }

    // Create MCP server with all tools
    auto server = claude::mcp::create_server("tools", "0.1.0", std::move(mcp_tools));

    // Update options with MCP handler and tools
    options_->sdk_mcp_handlers["tools"] = server;
    options_->allowed_tools = allowed_tool_names;

    // Set system prompt only for NEW sessions (not when resuming)
    if (session_id_.empty())
    {
        options_->system_prompt = system_prompt_;
        options_->resume.clear();
    }
    else
    {
        options_->system_prompt.clear();
        options_->resume = session_id_;
    }

    // Apply BYOK configuration if set
    if (byok_.is_configured())
    {
        // Set environment variables for authentication
        SET_ENV("ANTHROPIC_AUTH_TOKEN", byok_.api_key.c_str());
        if (!byok_.base_url.empty())
        {
            SET_ENV("ANTHROPIC_BASE_URL", byok_.base_url.c_str());
        }
        if (byok_.timeout_ms > 0)
        {
            SET_ENV("API_TIMEOUT_MS", std::to_string(byok_.timeout_ms).c_str());
        }

        // Set model programmatically
        if (!byok_.model.empty())
        {
            options_->model = byok_.model;
        }
    }

    // Create and connect client
    try
    {
        client_ = std::make_unique<claude::ClaudeClient>(*options_);
        client_->connect();
        connected_ = true;
        last_error_.clear();
        return true;
    }
    catch (const std::exception& e)
    {
        last_error_ = e.what();
        return false;
    }
    catch (...)
    {
        last_error_ = "Unknown connection error";
        return false;
    }
}

std::string ClaudeProvider::send_query(const std::string& query)
{
    // Delegate to callback version with no-op callback
    return send_query(query, nullptr);
}

std::string ClaudeProvider::send_query(const std::string& query, EventCallback callback)
{
    if (!ensure_connection())
    {
        if (!last_error_.empty())
        {
            return "Error: Failed to connect to Claude: " + last_error_;
        }
        return "Error: Failed to connect to Claude";
    }

    // Apply priming (for Claude) if needed: prepend stored system prompt once when it contains
    // newlines. This avoids CLI arg breakage for multiline prompts.
    std::string final_query;
    if (!primed_ && !system_prompt_.empty())
    {
        final_query = system_prompt_ + "\n\n---\n\n" + query;
        primed_ = true;
    }
    else
    {
        final_query = query;
    }

    // Send query
    client_->send_query(final_query);

    // Collect response
    std::string response;
    auto stream = client_->receive_messages();
    auto deadline = std::chrono::steady_clock::now() + response_timeout_;

    while (true)
    {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
        {
            try
            {
                client_->force_disconnect();
            }
            catch (...)
            {
            }
            connected_ = false;
            return "Error: Timed out waiting for response";
        }

        auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        auto next = stream.get_next_for(remaining);
        if (!next)
        {
            if (!stream.has_more())
                break;
            continue;
        }

        const auto& message = *next;
        if (claude::is_assistant_message(message))
        {
            const auto& assistant = std::get<claude::AssistantMessage>(message);

            // Check for assistant-level errors (auth, billing, rate limit, etc.)
            if (assistant.error.has_value())
            {
                return "Error: " + assistant_error_to_string(assistant.error.value());
            }

            for (const auto& block : assistant.content)
            {
                if (std::holds_alternative<claude::TextBlock>(block))
                {
                    const auto& text = std::get<claude::TextBlock>(block);
                    response += text.text;
                }
                else if (std::holds_alternative<claude::ToolUseBlock>(block))
                {
                    // Emit ToolCall event
                    if (callback)
                    {
                        const auto& tool_use = std::get<claude::ToolUseBlock>(block);
                        Event ev;
                        ev.type = EventType::ToolCall;
                        ev.tool_name = tool_use.name;
                        ev.tool_call_id = tool_use.id;
                        ev.tool_args = tool_use.input.dump();
                        callback(ev);
                    }
                }
                else if (std::holds_alternative<claude::ToolResultBlock>(block))
                {
                    // Emit ToolResult event
                    if (callback)
                    {
                        const auto& tool_result = std::get<claude::ToolResultBlock>(block);
                        Event ev;
                        ev.type = EventType::ToolResult;
                        ev.tool_call_id = tool_result.tool_use_id;
                        ev.tool_result = tool_result.content;
                        callback(ev);
                    }
                }
            }
        }
        else if (claude::is_result_message(message))
        {
            const auto& result = std::get<claude::ResultMessage>(message);

            // Check for result-level errors
            if (result.is_error())
            {
                // Try to extract error details from raw_json if available
                std::string error_detail;
                if (result.raw_json.contains("result") &&
                    result.raw_json["result"].is_string())
                {
                    error_detail = result.raw_json["result"].get<std::string>();
                }
                if (error_detail.empty())
                {
                    error_detail = "Query failed (subtype: error)";
                }
                return "Error: " + error_detail;
            }

            // Extract session_id from result for conversation continuity
            if (!result.session_id().empty())
            {
                session_id_ = result.session_id();
            }
            break;
        }
    }

    return response;
}

void ClaudeProvider::abort()
{
    if (client_ && connected_)
    {
        try
        {
            client_->interrupt();
        }
        catch (...)
        {
        }
    }
}

} // namespace libagents
