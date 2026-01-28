#include <libagents/agent.hpp>

#ifdef LIBAGENTS_HAS_COPILOT
#include <libagents/providers/copilot_provider.hpp>
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
#include <libagents/providers/claude_provider.hpp>
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace libagents
{
namespace
{
class HostedQueryBridge
{
  public:
    using ToolHandler = std::function<std::string(const std::string&)>;

    std::string dispatch_tool(const ToolHandler& handler, const std::string& args)
    {
        auto call = std::make_shared<ToolCall>();
        call->handler = handler;
        call->args = args;
        auto future = call->promise.get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tool_calls_.push_back(call);
        }
        cv_.notify_all();
        return future.get();
    }

    void enqueue_event(const Event& event)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(event);
        cv_.notify_all();
    }

    bool process_one(const HostContext& host)
    {
        std::shared_ptr<ToolCall> tool_call;
        Event event;
        bool has_event = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!tool_calls_.empty())
            {
                tool_call = tool_calls_.front();
                tool_calls_.pop_front();
            }
            else if (!events_.empty())
            {
                event = std::move(events_.front());
                events_.pop_front();
                has_event = true;
            }
            else
            {
                return false;
            }
        }

        if (tool_call)
        {
            std::string result;
            try
            {
                result = tool_call->handler(tool_call->args);
            }
            catch (const std::exception& e)
            {
                result = std::string("Error: ") + e.what();
            }
            catch (...)
            {
                result = "Error: Tool handler threw";
            }
            tool_call->promise.set_value(result);
            return true;
        }

        if (has_event && host.on_event)
            host.on_event(event);

        return has_event;
    }

    bool has_pending() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return !tool_calls_.empty() || !events_.empty();
    }

    void wait_for_pending(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, timeout, [this] { return !tool_calls_.empty() || !events_.empty(); });
    }

  private:
    struct ToolCall
    {
        ToolHandler handler;
        std::string args;
        std::promise<std::string> promise;
    };

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<ToolCall>> tool_calls_;
    std::deque<Event> events_;
};
} // namespace

/// Agent implementation that wraps a provider
class AgentImpl : public IAgent
{
  public:
    explicit AgentImpl(ProviderType type) : provider_type_(type) {}

    ~AgentImpl() override { shutdown(); }

    // ─────────────────────────────────────────────────────────────────────────
    // Configuration
    // ─────────────────────────────────────────────────────────────────────────

    void register_tool(const Tool& tool) override
    {
        tools_.push_back(tool);
        if (initialized_)
            apply_tools_to_provider();
    }

    void set_system_prompt(const std::string& prompt) override { system_prompt_ = prompt; }

    void set_model(const std::string& model) override { model_ = model; }

    void set_option(const std::string& key, const std::string& value) override
    {
        options_[key] = value;
    }

    void set_byok(const BYOKConfig& config) override { byok_ = config; }

    void set_response_timeout(std::chrono::milliseconds timeout) override
    {
        response_timeout_ = timeout;
        // If already initialized, update the provider
#ifdef LIBAGENTS_HAS_COPILOT
        if (copilot_provider_)
            copilot_provider_->set_response_timeout(timeout);
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
        if (claude_provider_)
            claude_provider_->set_response_timeout(timeout);
#endif
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Lifecycle
    // ─────────────────────────────────────────────────────────────────────────

    bool initialize() override
    {
        if (initialized_)
            return true;

        // Create and configure provider based on type
        switch (provider_type_)
        {
#ifdef LIBAGENTS_HAS_COPILOT
        case ProviderType::Copilot:
            copilot_provider_ = std::make_unique<CopilotProvider>();
            if (byok_.is_configured())
                copilot_provider_->set_byok(byok_);
            if (!copilot_provider_->initialize())
                return false;
            // Configure provider with tools and prompt
            copilot_provider_->register_tools(build_hosted_tools());
            copilot_provider_->set_system_prompt(system_prompt_);
            if (!session_id_.empty())
                copilot_provider_->set_session_id(session_id_);
            if (response_timeout_.count() > 0)
                copilot_provider_->set_response_timeout(response_timeout_);
            break;
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
        case ProviderType::Claude:
            claude_provider_ = std::make_unique<ClaudeProvider>();
            if (byok_.is_configured())
                claude_provider_->set_byok(byok_);
            if (!claude_provider_->initialize())
                return false;
            // Configure provider with tools and prompt
            claude_provider_->register_tools(build_hosted_tools());
            claude_provider_->set_system_prompt(system_prompt_);
            if (!session_id_.empty())
                claude_provider_->set_session_id(session_id_);
            if (response_timeout_.count() > 0)
                claude_provider_->set_response_timeout(response_timeout_);
            break;
#endif

        default:
            return false;
        }

        initialized_ = true;
        return true;
    }

    void shutdown() override
    {
#ifdef LIBAGENTS_HAS_COPILOT
        if (copilot_provider_)
        {
            copilot_provider_->shutdown();
            copilot_provider_.reset();
        }
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
        if (claude_provider_)
        {
            claude_provider_->shutdown();
            claude_provider_.reset();
        }
#endif

        initialized_ = false;
    }

    bool is_initialized() const override { return initialized_; }

    // ─────────────────────────────────────────────────────────────────────────
    // Query Interface
    // ─────────────────────────────────────────────────────────────────────────

    std::string query(const std::string& message) override
    {
        if (!initialized_)
            return "Error: Agent not initialized";

        switch (provider_type_)
        {
#ifdef LIBAGENTS_HAS_COPILOT
        case ProviderType::Copilot:
            return copilot_provider_->send_query(message);
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
        case ProviderType::Claude:
            return claude_provider_->send_query(message);
#endif

        default:
            return "Error: No provider available";
        }
    }

    std::string query_streaming(const std::string& message, EventCallback callback) override
    {
        if (!initialized_)
            return "Error: Agent not initialized";

        switch (provider_type_)
        {
#ifdef LIBAGENTS_HAS_COPILOT
        case ProviderType::Copilot:
            return copilot_provider_->send_query(message, callback);
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
        case ProviderType::Claude:
            return claude_provider_->send_query(message, callback);
#endif

        default:
            return "Error: No provider available";
        }
    }

    std::string query_hosted(const std::string& message, const HostContext& host) override
    {
        if (!initialized_)
            return "Error: Agent not initialized";

        HostedQueryBridge bridge;
        hosted_bridge_.store(&bridge);
        std::atomic<bool> abort_requested{false};
        busy_ = true;

        std::promise<std::string> response_promise;
        auto response_future = response_promise.get_future();

        EventCallback callback = nullptr;
        if (host.on_event)
            callback = [&bridge](const Event& event) { bridge.enqueue_event(event); };

        std::thread worker(
            [&]()
            {
                std::string response;
                try
                {
                    switch (provider_type_)
                    {
#ifdef LIBAGENTS_HAS_COPILOT
                    case ProviderType::Copilot:
                        response = copilot_provider_->send_query(message, callback);
                        break;
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
                    case ProviderType::Claude:
                        response = claude_provider_->send_query(message, callback);
                        break;
#endif

                    default:
                        response = "Error: No provider available";
                        break;
                    }
                }
                catch (const std::exception& e)
                {
                    response = std::string("Error: ") + e.what();
                }
                catch (...)
                {
                    response = "Error: Query failed";
                }
                response_promise.set_value(response);
            });

        while (response_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        {
            if (bridge.process_one(host))
                continue;

            if (host.should_abort && host.should_abort())
            {
                abort_requested = true;
                abort();
            }

            bridge.wait_for_pending(std::chrono::milliseconds(10));
        }

        std::string response = response_future.get();
        worker.join();
        hosted_bridge_.store(nullptr);

        while (bridge.process_one(host))
        {
        }

        if (host.on_event && !abort_requested)
        {
            Event event;
            event.type = EventType::ContentComplete;
            event.content = response;
            host.on_event(event);
        }

        busy_ = false;
        if (abort_requested)
            return "(Aborted)";
        return response;
    }

    void abort() override
    {
#ifdef LIBAGENTS_HAS_COPILOT
        if (copilot_provider_)
            copilot_provider_->abort();
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
        if (claude_provider_)
            claude_provider_->abort();
#endif
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Session Management
    // ─────────────────────────────────────────────────────────────────────────

    void clear_session() override
    {
#ifdef LIBAGENTS_HAS_COPILOT
        if (copilot_provider_)
            copilot_provider_->clear_session();
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
        if (claude_provider_)
            claude_provider_->clear_session();
#endif
    }

    std::string get_session_id() const override
    {
#ifdef LIBAGENTS_HAS_COPILOT
        if (copilot_provider_)
            return copilot_provider_->get_session_id();
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
        if (claude_provider_)
            return claude_provider_->get_session_id();
#endif
        return "";
    }

    void set_session_id(const std::string& session_id) override
    {
        session_id_ = session_id;
        // If already initialized, update the provider
#ifdef LIBAGENTS_HAS_COPILOT
        if (copilot_provider_)
            copilot_provider_->set_session_id(session_id);
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
        if (claude_provider_)
            claude_provider_->set_session_id(session_id);
#endif
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Info
    // ─────────────────────────────────────────────────────────────────────────

    std::string provider_name() const override { return provider_type_name(provider_type_); }

    bool is_busy() const override { return busy_; }

    std::string get_last_error() const override
    {
#ifdef LIBAGENTS_HAS_COPILOT
        if (provider_type_ == ProviderType::Copilot && copilot_provider_)
        {
            return copilot_provider_->get_last_error();
        }
#endif
#ifdef LIBAGENTS_HAS_CLAUDE
        if (provider_type_ == ProviderType::Claude && claude_provider_)
        {
            return claude_provider_->get_last_error();
        }
#endif
        return "";
    }

  private:
    std::vector<Tool> build_hosted_tools()
    {
        std::vector<Tool> wrapped;
        wrapped.reserve(tools_.size());
        for (const auto& tool : tools_)
        {
            Tool wrapped_tool = tool;
            if (tool.handler)
            {
                auto handler = tool.handler;
                wrapped_tool.handler = [this, handler](const std::string& args) -> std::string
                { return invoke_tool_handler(handler, args); };
            }
            wrapped.push_back(std::move(wrapped_tool));
        }
        return wrapped;
    }

    std::string invoke_tool_handler(const std::function<std::string(const std::string&)>& handler,
                                    const std::string& args)
    {
        HostedQueryBridge* bridge = hosted_bridge_.load();
        if (!bridge)
            return handler(args);
        return bridge->dispatch_tool(handler, args);
    }

    void apply_tools_to_provider()
    {
        auto wrapped = build_hosted_tools();
        switch (provider_type_)
        {
#ifdef LIBAGENTS_HAS_COPILOT
        case ProviderType::Copilot:
            if (copilot_provider_)
                copilot_provider_->register_tools(wrapped);
            break;
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
        case ProviderType::Claude:
            if (claude_provider_)
                claude_provider_->register_tools(wrapped);
            break;
#endif

        default:
            break;
        }
    }

    ProviderType provider_type_;
    bool initialized_ = false;
    bool busy_ = false;
    std::string system_prompt_;
    std::string model_;
    std::string session_id_;
    std::vector<Tool> tools_;
    std::unordered_map<std::string, std::string> options_;
    BYOKConfig byok_;
    std::chrono::milliseconds response_timeout_{std::chrono::seconds(60)};
    std::atomic<HostedQueryBridge*> hosted_bridge_{nullptr};

#ifdef LIBAGENTS_HAS_COPILOT
    std::unique_ptr<CopilotProvider> copilot_provider_;
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
    std::unique_ptr<ClaudeProvider> claude_provider_;
#endif
};

std::unique_ptr<IAgent> create_agent(ProviderType type)
{
    return std::make_unique<AgentImpl>(type);
}

} // namespace libagents
