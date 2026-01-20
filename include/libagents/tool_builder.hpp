#pragma once

/// @file tool_builder.hpp
/// @brief Type-safe tool builder with automatic JSON schema generation
///
/// Provides make_tool() function for creating tools from lambdas with
/// automatic parameter schema generation and type-safe invocation.
///
/// Example:
/// @code
/// auto echo = libagents::make_tool(
///     "echo", "Echo a message",
///     [](std::string message) { return message; },
///     {"message"}
/// );
///
/// auto add = libagents::make_tool(
///     "add", "Add two numbers",
///     [](double a, double b) { return std::to_string(a + b); },
///     {"a", "b"}
/// );
///
/// // Optional parameters (not added to "required" in schema)
/// auto greet = libagents::make_tool(
///     "greet", "Greet someone",
///     [](std::string name, std::optional<std::string> title) {
///         return title ? "Hello, " + *title + " " + name : "Hello, " + name;
///     },
///     {"name", "title"}
/// );
///
/// agent->register_tool(echo);
/// @endcode

#include "config.hpp"

#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <vector>

namespace libagents
{

namespace detail
{

// Remove cv-ref qualifiers
template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

// Detect std::optional
template <typename T>
struct is_optional : std::false_type
{
};

template <typename T>
struct is_optional<std::optional<T>> : std::true_type
{
};

template <typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

// JSON schema type mapping
template <typename T>
struct schema_type
{
    static json schema()
    {
        if constexpr (std::is_same_v<remove_cvref_t<T>, std::string>)
            return {{"type", "string"}};
        else if constexpr (std::is_same_v<remove_cvref_t<T>, int> ||
                           std::is_same_v<remove_cvref_t<T>, long> ||
                           std::is_same_v<remove_cvref_t<T>, long long>)
            return {{"type", "integer"}};
        else if constexpr (std::is_same_v<remove_cvref_t<T>, float> ||
                           std::is_same_v<remove_cvref_t<T>, double>)
            return {{"type", "number"}};
        else if constexpr (std::is_same_v<remove_cvref_t<T>, bool>)
            return {{"type", "boolean"}};
        else if constexpr (is_optional_v<remove_cvref_t<T>>)
            return schema_type<typename remove_cvref_t<T>::value_type>::schema();
        else
            return {{"type", "string"}}; // Default fallback
    }
};

// Extract argument from JSON
template <typename T>
T extract_arg(const json& args, const std::string& name)
{
    if constexpr (is_optional_v<std::decay_t<T>>)
    {
        using value_type = typename std::decay_t<T>::value_type;
        if (!args.contains(name) || args.at(name).is_null())
            return std::nullopt;
        return extract_arg<value_type>(args, name);
    }
    else if constexpr (std::is_same_v<std::decay_t<T>, std::string>)
    {
        return args.at(name).get<std::string>();
    }
    else
    {
        return args.at(name).get<std::decay_t<T>>();
    }
}

// Function traits for extracting lambda signature
template <typename T>
struct function_traits;

// Function pointer
template <typename R, typename... Args>
struct function_traits<R (*)(Args...)>
{
    using return_type = R;
    static constexpr size_t arity = sizeof...(Args);

    template <size_t N>
    using arg_type = std::tuple_element_t<N, std::tuple<Args...>>;
};

// Member function pointer
template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...)> : function_traits<R (*)(Args...)>
{
};

// Const member function pointer
template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...) const> : function_traits<R (*)(Args...)>
{
};

// Lambda/functor - extract via operator()
template <typename T>
struct function_traits : function_traits<decltype(&T::operator())>
{
};

// Invoke function with JSON args
template <typename Func, size_t... Is>
auto invoke_with_json_impl(Func&& func, const json& args, const std::vector<std::string>& names,
                           std::index_sequence<Is...>)
{
    using traits = function_traits<remove_cvref_t<Func>>;
    return func(extract_arg<remove_cvref_t<typename traits::template arg_type<Is>>>(args, names[Is])...);
}

template <typename Func>
auto invoke_with_json(Func&& func, const json& args, const std::vector<std::string>& names)
{
    using traits = function_traits<remove_cvref_t<Func>>;
    return invoke_with_json_impl<Func>(std::forward<Func>(func), args, names,
                                       std::make_index_sequence<traits::arity>{});
}

// Add to required array only if not optional
template <typename T>
void add_required_if(json& required, const std::string& name)
{
    if constexpr (!is_optional_v<T>)
    {
        required.push_back(name);
    }
}

// Generate schema from function signature
template <typename Func, size_t... Is>
json generate_schema_impl(const std::vector<std::string>& names, std::index_sequence<Is...>)
{
    using traits = function_traits<remove_cvref_t<Func>>;
    json schema = {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}};

    ((schema["properties"][names[Is]] =
          schema_type<remove_cvref_t<typename traits::template arg_type<Is>>>::schema(),
      add_required_if<remove_cvref_t<typename traits::template arg_type<Is>>>(schema["required"], names[Is])),
     ...);

    return schema;
}

template <typename Func>
json generate_schema(const std::vector<std::string>& names)
{
    using traits = function_traits<remove_cvref_t<Func>>;
    return generate_schema_impl<Func>(names, std::make_index_sequence<traits::arity>{});
}

// Convert result to string
template <typename T>
std::string to_result_string(const T& value)
{
    if constexpr (std::is_same_v<std::decay_t<T>, std::string>)
    {
        return value;
    }
    else if constexpr (std::is_arithmetic_v<std::decay_t<T>>)
    {
        return std::to_string(value);
    }
    else
    {
        return json(value).dump();
    }
}

} // namespace detail

/// Create a tool from a function with parameter names
///
/// @param name Tool name
/// @param description Tool description
/// @param func Handler function (lambda or callable)
/// @param param_names Parameter names (must match function arity)
/// @return Tool ready for registration
///
/// Example:
/// @code
/// auto tool = libagents::make_tool(
///     "echo", "Echo a message",
///     [](std::string message) { return message; },
///     {"message"}
/// );
/// @endcode
template <typename Func>
Tool make_tool(std::string name, std::string description, Func&& func,
               std::vector<std::string> param_names)
{
    using traits = detail::function_traits<detail::remove_cvref_t<Func>>;

    if (param_names.size() != traits::arity)
    {
        throw std::invalid_argument("Parameter name count mismatch for tool '" + name + "': expected " +
                                    std::to_string(traits::arity) + ", got " +
                                    std::to_string(param_names.size()));
    }

    Tool tool;
    tool.name = std::move(name);
    tool.description = std::move(description);
    tool.parameters_schema = detail::generate_schema<Func>(param_names).dump();

    // Capture func and param_names for the handler
    auto captured_func = std::make_shared<std::decay_t<Func>>(std::forward<Func>(func));
    auto captured_names = std::make_shared<std::vector<std::string>>(std::move(param_names));

    tool.handler = [captured_func, captured_names, tool_name = tool.name](const std::string& args_json) -> std::string
    {
        try
        {
            json args = json::parse(args_json);
            auto result = detail::invoke_with_json(*captured_func, args, *captured_names);
            return detail::to_result_string(result);
        }
        catch (const json::exception& e)
        {
            throw std::runtime_error("Tool '" + tool_name + "' JSON error: " + e.what());
        }
        catch (const std::out_of_range& e)
        {
            throw std::runtime_error("Tool '" + tool_name + "' missing required parameter: " + e.what());
        }
    };

    return tool;
}

} // namespace libagents
