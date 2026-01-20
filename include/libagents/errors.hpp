#pragma once

#include <stdexcept>
#include <string>

namespace libagents
{

/// Base exception for all libagents errors
class AgentError : public std::runtime_error
{
  public:
    explicit AgentError(const std::string& message) : std::runtime_error(message) {}
};

/// Provider initialization failed
class ProviderInitError : public AgentError
{
  public:
    explicit ProviderInitError(const std::string& message)
        : AgentError("Provider initialization failed: " + message)
    {
    }
};

/// Session creation failed
class SessionError : public AgentError
{
  public:
    explicit SessionError(const std::string& message) : AgentError("Session error: " + message) {}
};

/// Connection/communication error
class ConnectionError : public AgentError
{
  public:
    explicit ConnectionError(const std::string& message)
        : AgentError("Connection error: " + message)
    {
    }
};

/// Tool execution error
class ToolError : public AgentError
{
  public:
    explicit ToolError(const std::string& tool_name, const std::string& message)
        : AgentError("Tool '" + tool_name + "' error: " + message), tool_name_(tool_name)
    {
    }

    const std::string& tool_name() const { return tool_name_; }

  private:
    std::string tool_name_;
};

/// Request timeout
class TimeoutError : public AgentError
{
  public:
    explicit TimeoutError(const std::string& message) : AgentError("Timeout: " + message) {}
};

/// Invalid configuration
class ConfigError : public AgentError
{
  public:
    explicit ConfigError(const std::string& message) : AgentError("Configuration error: " + message)
    {
    }
};

} // namespace libagents
