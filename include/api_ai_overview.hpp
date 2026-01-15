#pragma once

#include <string>
#include "third_party/nlohmann/json.hpp"

namespace cord19 {

using json = nlohmann::json;

// Forward declarations
struct Engine;
class StatsTracker;

// Configuration for Azure OpenAI service
struct AzureOpenAIConfig {
    std::string endpoint;      // e.g., "https://your-resource.openai.azure.com"
    std::string api_key;
    std::string model;         // e.g., "gpt-5.2-chat"
    std::string api_version = "2024-02-15-preview"; // Azure OpenAI API version
};

// Generate an AI overview of search results using Azure OpenAI with caching
// Takes the search results JSON and returns an AI-generated overview
// Uses Engine's AI cache to save on API costs (24hr expiry, LRU eviction)
json generate_ai_overview(const AzureOpenAIConfig& config, 
                          const std::string& query,
                          int k,
                          const json& search_results,
                          Engine* engine,
                          StatsTracker* stats = nullptr);

} // namespace cord19
