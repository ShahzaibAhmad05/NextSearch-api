#include "api_ai_overview.hpp"
#include "api_engine.hpp"
#include "api_stats.hpp"
#include <iostream>
#include <sstream>
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace cord19 {

// Helper function to construct the system prompt for AI overview generation
static std::string build_system_prompt() {
    return R"(You are an AI assistant that generates short, informative overviews of search results in proper markdown format with headings and newline chars.

    Your task is to analyze the provided search results and create a comprehensive summary that:

    1. Answers the user's query directly
    2. Synthesizes information from multiple sources
    3. Highlights key findings and relevant details
    4. Maintains accuracy and avoids speculation
    5. Cites specific documents when appropriate

    TO SUCCEED, FOLLOW THESE RULES:
    - The first paragraph should directly answer the user's query.
    - Add a horizontal rule (---) after the first paragraph.
    - Format it in proper markdown,
    - Use appropriate markdown headings wherever needed.)";
}

// Helper function to build the user prompt with search results
static std::string build_user_prompt(const std::string& query, const json& search_results) {
    std::ostringstream oss;
    oss << "User Query: " << query << "\n\n";
    oss << "Search Results:\n\n";
    
    // Extract and format the search results
    if (search_results.contains("results") && search_results["results"].is_array()) {
        const auto& results = search_results["results"];
        int rank = 1;
        
        for (const auto& result : results) {
            oss << "Document " << rank << ":\n";
            
            if (result.contains("title")) {
                oss << "Title: " << result["title"].get<std::string>() << "\n";
            }
            
            if (result.contains("cord_uid")) {
                oss << "ID: " << result["cord_uid"].get<std::string>() << "\n";
            }
            
            if (result.contains("bm25_score")) {
                oss << "Relevance Score: " << result["bm25_score"].get<double>() << "\n";
            }
            
            if (result.contains("url")) {
                oss << "URL: " << result["url"].get<std::string>() << "\n";
            }
            
            if (result.contains("author")) {
                oss << "Author: " << result["author"].get<std::string>() << "\n";
            }
            
            if (result.contains("publish_time")) {
                oss << "Published: " << result["publish_time"].get<std::string>() << "\n";
            }
            
            oss << "\n";
            rank++;
        }
    }
    
    oss << "Please provide a comprehensive AI overview based on these search results.";
    return oss.str();
}

// Helper function to make HTTPS POST request using WinHTTP
static std::string make_https_post(const std::string& url, const std::string& path, 
                                   const std::string& api_key, const std::string& body) {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    std::string response;
    
    try {
        // Parse URL to extract host
        std::string host = url;
        if (host.find("https://") == 0) {
            host = host.substr(8);
        }
        if (host.back() == '/') {
            host.pop_back();
        }
        
        // Convert to wide strings
        std::wstring whost(host.begin(), host.end());
        std::wstring wpath(path.begin(), path.end());
        
        // Initialize WinHTTP
        hSession = WinHttpOpen(L"AzureOpenAI/1.0",
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
        
        if (!hSession) return "";
        
        hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        hRequest = WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(),
                                     NULL, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     WINHTTP_FLAG_SECURE);
        
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        // Set headers
        std::string headers_str = "Content-Type: application/json\r\n";
        headers_str += "api-key: " + api_key + "\r\n";
        std::wstring wheaders(headers_str.begin(), headers_str.end());
        
        // Send request
        BOOL bResults = WinHttpSendRequest(hRequest,
                                          wheaders.c_str(),
                                          (DWORD)-1,
                                          (LPVOID)body.c_str(),
                                          (DWORD)body.length(),
                                          (DWORD)body.length(),
                                          0);
        
        if (bResults) {
            bResults = WinHttpReceiveResponse(hRequest, NULL);
        }
        
        if (bResults) {
            DWORD dwSize = 0;
            do {
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                
                if (dwSize > 0) {
                    std::vector<char> buffer(dwSize + 1, 0);
                    DWORD dwDownloaded = 0;
                    if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                        response.append(buffer.data(), dwDownloaded);
                    }
                }
            } while (dwSize > 0);
        }
        
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
    } catch (...) {
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
    }
    
    return response;
}

json generate_ai_overview(const AzureOpenAIConfig& config,
                          const std::string& query,
                          int k,
                          const json& search_results,
                          Engine* engine,
                          StatsTracker* stats,
                          bool is_authorized) {
    json response_json;
    
    // Track AI overview call
    if (stats) {
        stats->increment_ai_overview_calls();
    }
    
    // Check cache first if engine is provided
    if (engine) {
        std::string cache_key = engine->make_cache_key(query, k);
        
        std::lock_guard<std::mutex> lock(engine->mtx);
        json cached = engine->get_ai_overview_from_cache(cache_key);
        
        if (!cached.is_null() && cached.contains("from_cache")) {
            std::cerr << "[ai_overview] Cache HIT for query: \"" << query << "\" k=" << k << "\n";
            
            // Track cache hit
            if (stats) {
                stats->increment_ai_overview_cache_hits();
            }
            
            // Remove internal flag and add user-visible flag
            cached.erase("from_cache");
            cached["cached"] = true;
            return cached;
        }
        
        std::cerr << "[ai_overview] Cache MISS for query: \"" << query << "\" k=" << k << "\n";
    }
    
    try {
        // Build the API path
        std::string path = "/openai/deployments/" + config.model + 
                          "/chat/completions?api-version=" + config.api_version;
        
        // Build the request body
        json request_body;
        request_body["messages"] = json::array();
        
        // Add system message
        json system_msg;
        system_msg["role"] = "system";
        system_msg["content"] = build_system_prompt();
        request_body["messages"].push_back(system_msg);
        
        // Add user message with query and results
        json user_msg;
        user_msg["role"] = "user";
        user_msg["content"] = build_user_prompt(query, search_results);
        request_body["messages"].push_back(user_msg);
        
        // Set parameters
        request_body["max_completion_tokens"] = 1000;
        
        std::string body_str = request_body.dump();
        
        std::cerr << "[azure_openai] Calling Azure OpenAI at " << config.endpoint << path << "\n";
        
        // Decrement AI API calls remaining only for unauthorized requests
        if (stats && !is_authorized) {
            stats->decrement_ai_api_calls();
            std::cerr << "[azure_openai] Unauthorized request - decrementing counter\n";
        } else if (is_authorized) {
            std::cerr << "[azure_openai] Authorized request - counter not decremented\n";
        }
        
        // Make the HTTPS POST request using WinHTTP
        std::string response_body = make_https_post(config.endpoint, path, config.api_key, body_str);
        
        if (response_body.empty()) {
            response_json["error"] = "Failed to connect to Azure OpenAI";
            response_json["success"] = false;
            std::cerr << "[azure_openai] Connection failed\n";
            return response_json;
        }
        
        // Parse the response
        json api_response = json::parse(response_body);
        
        // Check for API errors
        if (api_response.contains("error")) {
            response_json["error"] = "Azure OpenAI API error";
            response_json["details"] = api_response["error"];
            response_json["success"] = false;
            std::cerr << "[azure_openai] API error: " << api_response.dump() << "\n";
            return response_json;
        }
        
        // Extract the AI overview from the response
        if (api_response.contains("choices") && 
            api_response["choices"].is_array() && 
            !api_response["choices"].empty()) {
            
            const auto& choice = api_response["choices"][0];
            if (choice.contains("message") && 
                choice["message"].contains("content")) {
                
                response_json["success"] = true;
                response_json["overview"] = choice["message"]["content"];
                response_json["model"] = config.model;
                response_json["cached"] = false;
                
                // Include token usage if available
                if (api_response.contains("usage")) {
                    response_json["usage"] = api_response["usage"];
                }
                
                std::cerr << "[azure_openai] Successfully generated AI overview\n";
                
                // Cache the successful response if engine is provided
                if (engine) {
                    std::string cache_key = engine->make_cache_key(query, k);
                    std::lock_guard<std::mutex> lock(engine->mtx);
                    engine->put_ai_overview_in_cache(cache_key, response_json);
                    std::cerr << "[ai_overview] Cached AI overview for query: \"" << query << "\" k=" << k << "\n";
                }
            } else {
                response_json["error"] = "Unexpected response structure";
                response_json["success"] = false;
            }
        } else {
            response_json["error"] = "No choices in response";
            response_json["success"] = false;
        }
        
    } catch (const std::exception& e) {
        response_json["error"] = std::string("Exception: ") + e.what();
        response_json["success"] = false;
        std::cerr << "[azure_openai] Exception: " << e.what() << "\n";
    }
    
    return response_json;
}

} // namespace cord19
