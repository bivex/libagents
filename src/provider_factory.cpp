#include <libagents/provider.hpp>

#ifdef LIBAGENTS_HAS_COPILOT
#include <libagents/providers/copilot_provider.hpp>
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
#include <libagents/providers/claude_provider.hpp>
#endif

#include <stdexcept>

namespace libagents
{

std::unique_ptr<IProvider> create_provider(ProviderType type)
{
    switch (type)
    {
#ifdef LIBAGENTS_HAS_COPILOT
    case ProviderType::Copilot:
        return std::make_unique<CopilotProvider>();
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
    case ProviderType::Claude:
        return std::make_unique<ClaudeProvider>();
#endif

    default:
        throw std::runtime_error("Provider type not available or not compiled in");
    }
}

} // namespace libagents
