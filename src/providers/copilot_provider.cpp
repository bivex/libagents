#include <copilot/copilot.hpp>
#include <libagents/internal/response_waiter.hpp>
#include <libagents/providers/copilot_provider.hpp>

#include <string>
#include <vector>

namespace libagents
{

CopilotProvider::CopilotProvider() = default;

CopilotProvider::~CopilotProvider()
{
    shutdown();
}

bool CopilotProvider::initialize(const ProviderConfig& config)
{
    (void)config; // Not used yet, copilot_config_ used instead

    if (initialized_)
        return true;

    try
    {
        copilot::ClientOptions opts;
        opts.log_level = copilot_config_.log_level;
        opts.use_stdio = copilot_config_.use_stdio;
        opts.cli_args = copilot_config_.cli_args;

        client_ = std::make_unique<copilot::Client>(opts);
        client_->start().get();
        initialized_ = true;
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

void CopilotProvider::shutdown()
{
    // Destroy session first
    if (session_)
    {
        try
        {
            session_->destroy().get();
        }
        catch (...)
        {
        }
        session_.reset();
    }

    if (client_ && initialized_)
    {
        try
        {
            client_->stop().get();
        }
        catch (...)
        {
        }
        client_.reset();
    }
    initialized_ = false;
}

void CopilotProvider::clear_session()
{
    // Destroy current session to start fresh
    if (session_)
    {
        try
        {
            session_->destroy().get();
        }
        catch (...)
        {
        }
        session_.reset();
    }
    session_id_.clear();
}

std::unique_ptr<ISession> CopilotProvider::create_session(const SessionConfig& config)
{
    // TODO: Implement proper ISession wrapper
    // For now, use send_query() as the main interface
    (void)config;
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────────────────────

void CopilotProvider::register_tools(const std::vector<Tool>& tools)
{
    // Store tools and handlers
    registered_tools_ = tools;
    current_tool_handlers_.clear();

    for (const auto& tool : tools)
    {
        if (tool.handler)
        {
            current_tool_handlers_[tool.name] = tool.handler;
        }
    }

    // If we have an active session, it needs to be recreated to use new tools
    if (session_)
    {
        try
        {
            session_->destroy().get();
        }
        catch (...)
        {
        }
        session_.reset();
    }
}

void CopilotProvider::set_system_prompt(const std::string& prompt)
{
    system_prompt_ = prompt;
}

void CopilotProvider::set_response_timeout(std::chrono::milliseconds timeout)
{
    response_timeout_ = timeout;
}

void CopilotProvider::set_session_id(const std::string& session_id)
{
    session_id_ = session_id;
}

void CopilotProvider::set_byok(const BYOKConfig& config)
{
    byok_ = config;
}

// ─────────────────────────────────────────────────────────────────────────────
// Query Interface
// ─────────────────────────────────────────────────────────────────────────────

bool CopilotProvider::ensure_session()
{
    if (session_)
        return true;

    if (!client_ || !initialized_)
        return false;

    // Convert registered tools to SDK tools
    std::vector<copilot::Tool> sdk_tools;
    for (const auto& tool : registered_tools_)
    {
        copilot::Tool sdk_tool;
        sdk_tool.name = tool.name;
        sdk_tool.description = tool.description;

        // Use the actual schema if provided, otherwise create a generic one
        if (!tool.parameters_schema.empty())
        {
            try
            {
                sdk_tool.parameters_schema = copilot::json::parse(tool.parameters_schema);
            }
            catch (...)
            {
                // Fallback to generic schema
                sdk_tool.parameters_schema = copilot::json{
                    {"type", "object"},
                    {"properties", {{"args", {{"type", "string"}}}}},
                };
            }
        }

        // Set up handler
        sdk_tool.handler = [this, name = tool.name](const copilot::ToolInvocation& inv)
            -> copilot::ToolResultObject
        {
            copilot::ToolResultObject result;
            auto it = current_tool_handlers_.find(name);
            if (it != current_tool_handlers_.end())
            {
                std::string args_str = inv.arguments ? inv.arguments->dump() : "{}";
                result.text_result_for_llm = it->second(args_str);
            }
            else
            {
                result.result_type = "error";
                result.error = "Tool handler not found";
            }
            return result;
        };

        sdk_tools.push_back(sdk_tool);
    }

    // Try to resume if we have a session_id
    if (!session_id_.empty())
    {
        try
        {
            copilot::ResumeSessionConfig resume_config;
            resume_config.tools = sdk_tools;
            session_ = client_->resume_session(session_id_, resume_config).get();
            session_id_ = session_->session_id();
            return true;
        }
        catch (...)
        {
            session_.reset();
            // Fall through to create new session
        }
    }

    // Create new session
    try
    {
        copilot::SessionConfig config;
        config.tools = sdk_tools;
        config.system_message = copilot::SystemMessageConfig{
            .mode = copilot::SystemMessageMode::Replace, .content = system_prompt_};

        // Apply BYOK configuration if set
        if (byok_.is_configured())
        {
            copilot::ProviderConfig provider;
            provider.api_key = byok_.api_key;
            if (!byok_.base_url.empty())
            {
                provider.base_url = byok_.base_url;
            }
            if (!byok_.provider_type.empty())
            {
                provider.type = byok_.provider_type;
            }
            else
            {
                provider.type = "openai"; // Default to OpenAI
            }
            config.provider = provider;

            if (!byok_.model.empty())
            {
                config.model = byok_.model;
            }
        }

        session_ = client_->create_session(config).get();
        session_id_ = session_->session_id();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string CopilotProvider::send_query(const std::string& query)
{
    // Delegate to callback version with no-op callback
    return send_query(query, nullptr);
}

std::string CopilotProvider::send_query(const std::string& query, EventCallback callback)
{
    if (!ensure_session())
    {
        return "Error: Failed to create session";
    }

    // Event handling for collecting response
    detail::ResponseWaiter waiter;

    auto sub = session_->on(
        [&](const copilot::SessionEvent& event)
        {
            if (auto* msg = event.try_as<copilot::AssistantMessageData>())
            {
                waiter.append_response(msg->content);
            }
            else if (auto* tool_start = event.try_as<copilot::ToolExecutionStartData>())
            {
                // Emit ToolCall event
                if (callback)
                {
                    Event ev;
                    ev.type = EventType::ToolCall;
                    ev.tool_name = tool_start->tool_name;
                    ev.tool_call_id = tool_start->tool_call_id;
                    if (tool_start->arguments)
                    {
                        ev.tool_args = tool_start->arguments->dump();
                    }
                    callback(ev);
                }
            }
            else if (auto* tool_complete = event.try_as<copilot::ToolExecutionCompleteData>())
            {
                // Emit ToolResult event
                if (callback)
                {
                    Event ev;
                    ev.type = EventType::ToolResult;
                    ev.tool_call_id = tool_complete->tool_call_id;
                    if (tool_complete->result)
                    {
                        ev.tool_result = tool_complete->result->content;
                    }
                    if (tool_complete->error)
                    {
                        ev.tool_result = "Error: " + tool_complete->error->message;
                    }
                    callback(ev);
                }
            }
            else if (auto* error = event.try_as<copilot::SessionErrorData>())
            {
                waiter.set_error(error->message);
            }
            else if (auto* abort_data = event.try_as<copilot::AbortData>())
            {
                waiter.set_error("Aborted: " + abort_data->reason);
            }
            else if (event.type == copilot::SessionEventType::SessionIdle)
            {
                waiter.mark_done();
            }
        });

    // Send query
    copilot::MessageOptions msg_opts;
    msg_opts.prompt = query;
    session_->send(msg_opts).get();

    // Wait for completion
    auto result = waiter.wait_for(response_timeout_);

    if (!result.completed)
        return "Error: Timed out waiting for response";

    if (!result.error_message.empty())
        return "Error: " + result.error_message;

    return result.response;
}

void CopilotProvider::abort()
{
    if (session_)
    {
        try
        {
            session_->abort().get();
        }
        catch (...)
        {
        }
    }
}

std::string CopilotProvider::get_session_id() const
{
    return session_id_;
}

} // namespace libagents
