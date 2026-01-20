#pragma once

#include <functional>
#include <string>

namespace libagents
{

/// Unified event types across all providers
enum class EventType
{
    ContentDelta,    // Partial assistant response (streaming)
    ContentComplete, // Full assistant message done
    ToolCall,        // AI wants to call a tool
    ToolResult,      // Tool execution result
    Error,           // Error occurred
    SessionIdle      // Ready for next message
};

/// Event data for callbacks
struct Event
{
    EventType type;

    std::string content;       // For ContentDelta/ContentComplete
    std::string tool_name;     // For ToolCall
    std::string tool_args;     // For ToolCall (JSON)
    std::string tool_call_id;  // For ToolCall/ToolResult correlation
    std::string tool_result;   // For ToolResult
    std::string error_message; // For Error
    int error_code = 0;        // For Error
};

/// Callback type for receiving events
using EventCallback = std::function<void(const Event&)>;

/// RAII subscription handle - unsubscribes on destruction
class Subscription
{
  public:
    virtual ~Subscription() = default;

    /// Check if still subscribed
    virtual bool is_active() const = 0;

    /// Manually unsubscribe
    virtual void unsubscribe() = 0;
};

} // namespace libagents
