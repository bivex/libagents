#pragma once

#include <memory>
#include <string>

#include "config.hpp"
#include "session.hpp"

namespace libagents
{

/// Provider type enumeration
enum class ProviderType
{
    Copilot, // GitHub Copilot
    Claude   // Anthropic Claude
};

/// Provider interface for AI agent backends
class IProvider
{
  public:
    virtual ~IProvider() = default;

    /// Provider name (e.g., "copilot", "claude")
    virtual std::string name() const = 0;

    /// Initialize provider with configuration
    virtual bool initialize(const ProviderConfig& config = {}) = 0;

    /// Shutdown provider and release resources
    virtual void shutdown() = 0;

    /// Check if provider is initialized and ready
    virtual bool is_initialized() const = 0;

    /// Create a new session with the given configuration
    virtual std::unique_ptr<ISession> create_session(const SessionConfig& config) = 0;
};

/// Factory function to create a provider instance
std::unique_ptr<IProvider> create_provider(ProviderType type);

/// Get string name for provider type
inline const char* provider_type_name(ProviderType type)
{
    switch (type)
    {
    case ProviderType::Copilot:
        return "copilot";
    case ProviderType::Claude:
        return "claude";
    default:
        return "unknown";
    }
}

/// Parse provider type from string
inline ProviderType parse_provider_type(const std::string& name)
{
    if (name == "copilot" || name == "Copilot")
        return ProviderType::Copilot;
    if (name == "claude" || name == "Claude")
        return ProviderType::Claude;
    return ProviderType::Claude; // Default
}

} // namespace libagents
