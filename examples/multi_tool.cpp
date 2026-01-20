/// @file multi_tool.cpp
/// @brief Register multiple tools with automatic schema generation
///
/// Usage: multi_tool [claude|copilot]

#include <libagents/libagents.hpp>
#include <iostream>
#include <string>
#include <cmath>
#include <cstdlib>
#include <ctime>

int main(int argc, char* argv[])
{
    // Parse provider from command line (default: claude)
    auto provider = libagents::ProviderType::Claude;
    if (argc > 1)
        provider = libagents::parse_provider_type(argv[1]);

    std::cout << "Using provider: " << libagents::provider_type_name(provider) << std::endl;

    std::srand(static_cast<unsigned>(std::time(nullptr)));

    auto agent = libagents::create_agent(provider);

    // Tool with no args
    agent->register_tool(libagents::make_tool(
        "random", "Get a random number between 1 and 100",
        []() { return std::to_string(std::rand() % 100 + 1); },
        {}
    ));

    // Tool with optional arg
    agent->register_tool(libagents::make_tool(
        "greet", "Greet someone. Pass 'name' and optionally 'title' (e.g., Dr., Mr., Ms.)",
        [](std::string name, std::optional<std::string> title) {
            return title ? "Hello, " + *title + " " + name + "!"
                         : "Hello, " + name + "!";
        },
        {"name", "title"}
    ));

    // Tool with multiple numeric args
    agent->register_tool(libagents::make_tool(
        "distance", "Calculate distance between two points",
        [](int x1, int y1, int x2, int y2) {
            double d = std::sqrt(std::pow(x2 - x1, 2) + std::pow(y2 - y1, 2));
            return std::to_string(d);
        },
        {"x1", "y1", "x2", "y2"}
    ));

    agent->set_system_prompt(
        "You have tools: 'random' (no args), 'greet' (name, optional title), "
        "'distance' (x1,y1,x2,y2). Use them when asked. Be concise.");

    if (!agent->initialize())
    {
        std::cerr << "Failed to initialize agent" << std::endl;
        return 1;
    }

    // Use HostContext to see tool calls
    libagents::HostContext host;
    host.on_event = [](const libagents::Event& ev) {
        if (ev.type == libagents::EventType::ContentDelta)
            std::cout << ev.content << std::flush;
        else if (ev.type == libagents::EventType::ToolCall)
            std::cout << "\n  [tool: " << ev.tool_name << " args: " << ev.tool_args << "]\n";
    };

    std::cout << "\nQuery: Greet Dr. Smith, then calculate distance from (0,0) to (3,4)\n" << std::endl;

    agent->query_hosted(
        "First use the greet tool with name='Smith' and title='Dr.'. "
        "Then use the distance tool with x1=0, y1=0, x2=3, y2=4.",
        host
    );

    std::cout << std::endl;
    return 0;
}
