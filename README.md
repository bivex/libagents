# libagents

C++ library for building AI-powered agents with tool support. Provides a unified interface for Claude and GitHub Copilot providers.

## Features

- **Multi-provider**: Claude (via Claude Code CLI) and GitHub Copilot
- **Tool registration**: Define tools the AI can invoke
- **Type-safe tool builder**: `make_tool()` with automatic JSON schema generation
- **Session management**: Conversation continuity with session IDs
- **Event callbacks**: Tool call and completion notifications
- **BYOK support**: Bring your own API keys

## Quick Start

```cpp
#include <libagents/libagents.hpp>

int main() {
    // Create agent
    auto agent = libagents::create_agent(libagents::ProviderType::Claude);

    // Register a tool
    agent->register_tool(libagents::make_tool(
        "calculate", "Evaluate a math expression",
        [](std::string expr) { return std::to_string(eval(expr)); },
        {"expression"}
    ));

    // Initialize and query
    agent->set_system_prompt("You are a calculator assistant.");
    agent->initialize();

    std::string response = agent->query("What is 2 + 2 * 3?");
    std::cout << response << std::endl;
}
```

## Building

```bash
cmake -B build
cmake --build build
```

Requires: C++20, CMake 3.20+

## Integration

```cmake
add_subdirectory(libagents)
target_link_libraries(myapp PRIVATE libagents)
```

## Providers

| Provider | Backend | Auth |
|----------|---------|------|
| Claude | Claude Code CLI (`claude`) | Claude CLI login or BYOK |
| Copilot | GitHub Copilot | VS Code Copilot extension |

## API Overview

```cpp
// High-level agent interface
auto agent = libagents::create_agent(ProviderType::Claude);
agent->register_tool(tool);
agent->set_system_prompt("...");
agent->set_response_timeout(std::chrono::minutes(2));
agent->initialize();

std::string response = agent->query("Hello");
std::string response = agent->query_streaming(msg, callback);
std::string response = agent->query_hosted(msg, host_context);

agent->clear_session();
agent->set_session_id(id);
```

## Type-Safe Tools

```cpp
// Automatic JSON schema generation from lambda signature
auto tool = libagents::make_tool(
    "greet", "Greet someone",
    [](std::string name, std::optional<std::string> title) {
        return title ? "Hello, " + *title + " " + name
                     : "Hello, " + name;
    },
    {"name", "title"}  // "title" is optional in schema
);
```

## Examples

```bash
./basic_agent [claude|copilot]   # Minimal example
./hosted_query [claude|copilot]  # Main-thread tool dispatch
./events [claude|copilot]        # Event callbacks
./multi_tool [claude|copilot]    # Multiple tools
```

## Projects Using This Library

| Project | Description |
|---------|-------------|
| [windbg_copilot](https://github.com/0xeb/windbg_copilot) | WinDbg extension for AI-assisted debugging |
| [lldb_copilot](https://github.com/0xeb/lldb_copilot) | LLDB plugin for AI-assisted debugging |

Want to add your project? Open a PR!

## Author

Elias Bachaalany ([@0xeb](https://github.com/0xeb))

## License

MIT License - see [LICENSE](LICENSE) for details.
