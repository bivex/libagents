/// @file events.cpp
/// @brief Query with event callbacks
///
/// Usage: events [claude|copilot]

#include <libagents/libagents.hpp>
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    // Parse provider from command line (default: claude)
    auto provider = libagents::ProviderType::Claude;
    if (argc > 1)
        provider = libagents::parse_provider_type(argv[1]);

    std::cout << "Using provider: " << libagents::provider_type_name(provider) << std::endl;

    auto agent = libagents::create_agent(provider);
    agent->set_system_prompt("You are a helpful assistant. Be concise.");

    if (!agent->initialize())
    {
        std::cerr << "Failed to initialize agent" << std::endl;
        return 1;
    }

    std::cout << "\nResponse: " << std::flush;

    // Use query_hosted with event callbacks
    libagents::HostContext host;
    host.on_event = [](const libagents::Event& ev) {
        if (ev.type == libagents::EventType::ContentComplete)
            std::cout << ev.content << std::flush;
    };

    agent->query_hosted("Write a haiku about programming.", host);

    std::cout << std::endl;
    return 0;
}
