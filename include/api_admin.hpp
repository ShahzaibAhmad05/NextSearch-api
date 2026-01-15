#pragma once

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifdef NEXTSEARCH_HAVE_OPENSSL
#include <openssl/evp.h>
#include <openssl/hmac.h>
#endif

#include "third_party/nlohmann/json.hpp"

namespace cord19 {

using json = nlohmann::json;

#ifdef NEXTSEARCH_HAVE_OPENSSL

// Base64 URL-safe encoding (for JWT)
inline std::string base64_url_encode(const std::string& input) {
    static const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static const char* base64_url_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    
    std::string encoded;
    int val = 0;
    int valb = -6;
    
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            encoded.push_back(base64_url_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    
    if (valb > -6) {
        encoded.push_back(base64_url_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    
    // Remove padding for URL-safe JWT
    return encoded;
}

// Base64 URL-safe decoding
inline std::string base64_url_decode(const std::string& input) {
    static const int decode_table[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
        -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, 63,
        -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
    };
    
    std::string decoded;
    int val = 0;
    int valb = -8;
    
    for (unsigned char c : input) {
        if (c == '=') break; // Padding
        if (c == '-') c = '+'; // Convert URL-safe to standard
        if (c == '_') c = '/'; // Convert URL-safe to standard
        
        int d = decode_table[c];
        if (d == -1) continue; // Skip invalid characters
        
        val = (val << 6) + d;
        valb += 6;
        
        if (valb >= 0) {
            decoded.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    
    return decoded;
}

// HMAC SHA-256 signature
inline std::string hmac_sha256(const std::string& key, const std::string& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    
    HMAC(EVP_sha256(), 
         key.c_str(), key.length(),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         hash, &hash_len);
    
    return std::string(reinterpret_cast<char*>(hash), hash_len);
}

// JWT token generation
inline std::string generate_jwt_token(const std::string& secret, int expiration_seconds = 3600) {
    // Header
    json header;
    header["alg"] = "HS256";
    header["typ"] = "JWT";
    std::string header_str = header.dump();
    std::string encoded_header = base64_url_encode(header_str);
    
    // Payload
    auto now = std::chrono::system_clock::now();
    auto iat = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    auto exp = iat + expiration_seconds;
    
    json payload;
    payload["role"] = "admin";
    payload["iat"] = iat;
    payload["exp"] = exp;
    std::string payload_str = payload.dump();
    std::string encoded_payload = base64_url_encode(payload_str);
    
    // Signature
    std::string message = encoded_header + "." + encoded_payload;
    std::string signature = hmac_sha256(secret, message);
    std::string encoded_signature = base64_url_encode(signature);
    
    // JWT token
    return encoded_header + "." + encoded_payload + "." + encoded_signature;
}

// JWT token validation
struct JWTValidationResult {
    bool valid = false;
    std::string error;
    json payload;
};

inline JWTValidationResult validate_jwt_token(const std::string& token, const std::string& secret) {
    JWTValidationResult result;
    
    // Split token into parts
    std::vector<std::string> parts;
    size_t start = 0;
    size_t end = token.find('.');
    
    while (end != std::string::npos) {
        parts.push_back(token.substr(start, end - start));
        start = end + 1;
        end = token.find('.', start);
    }
    parts.push_back(token.substr(start));
    
    if (parts.size() != 3) {
        result.error = "Invalid token format";
        return result;
    }
    
    std::string encoded_header = parts[0];
    std::string encoded_payload = parts[1];
    std::string encoded_signature = parts[2];
    
    // Verify signature
    std::string message = encoded_header + "." + encoded_payload;
    std::string expected_signature = hmac_sha256(secret, message);
    std::string expected_encoded_signature = base64_url_encode(expected_signature);
    
    if (encoded_signature != expected_encoded_signature) {
        result.error = "Invalid signature";
        return result;
    }
    
    // Decode and parse payload
    try {
        std::string payload_str = base64_url_decode(encoded_payload);
        result.payload = json::parse(payload_str);
    } catch (const std::exception& e) {
        result.error = "Invalid payload: " + std::string(e.what());
        return result;
    }
    
    // Check expiration
    if (!result.payload.contains("exp")) {
        result.error = "Missing expiration claim";
        return result;
    }
    
    auto now = std::chrono::system_clock::now();
    auto current_time = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    int64_t exp = result.payload["exp"];
    
    if (current_time >= exp) {
        result.error = "Token expired";
        return result;
    }
    
    // Check role
    if (!result.payload.contains("role") || result.payload["role"] != "admin") {
        result.error = "Invalid role";
        return result;
    }
    
    result.valid = true;
    return result;
}

// Extract Bearer token from Authorization header
inline std::string extract_bearer_token(const std::string& auth_header) {
    if (auth_header.empty()) {
        return "";
    }
    
    // Check for "Bearer " prefix (case-insensitive)
    const std::string bearer_prefix = "Bearer ";
    if (auth_header.size() < bearer_prefix.size()) {
        return "";
    }
    
    std::string prefix = auth_header.substr(0, bearer_prefix.size());
    // Simple case-insensitive comparison
    for (size_t i = 0; i < prefix.size(); i++) {
        if (std::tolower(prefix[i]) != std::tolower(bearer_prefix[i])) {
            return "";
        }
    }
    
    return auth_header.substr(bearer_prefix.size());
}

// Authentication middleware for HTTP requests
inline bool require_admin_auth(const httplib::Request& req, httplib::Response& res, const std::string& jwt_secret) {
    // Get Authorization header
    if (!req.has_header("Authorization")) {
        res.status = 401;
        json err;
        err["error"] = "Unauthorized";
        res.set_content(err.dump(2), "application/json");
        return false;
    }
    
    std::string auth_header = req.get_header_value("Authorization");
    std::string token = extract_bearer_token(auth_header);
    
    if (token.empty()) {
        res.status = 401;
        json err;
        err["error"] = "Unauthorized";
        res.set_content(err.dump(2), "application/json");
        return false;
    }
    
    // Validate token
    auto validation_result = validate_jwt_token(token, jwt_secret);
    
    if (!validation_result.valid) {
        res.status = 401;
        json err;
        err["error"] = "Unauthorized";
        res.set_content(err.dump(2), "application/json");
        return false;
    }
    
    return true;
}

#else // !NEXTSEARCH_HAVE_OPENSSL

// Stub implementations when OpenSSL is not available
inline std::string generate_jwt_token(const std::string& secret, int expiration_seconds = 3600) {
    return ""; // Not supported without OpenSSL
}

struct JWTValidationResult {
    bool valid = false;
    std::string error = "OpenSSL not available";
    json payload;
};

inline JWTValidationResult validate_jwt_token(const std::string& token, const std::string& secret) {
    JWTValidationResult result;
    result.error = "Admin authentication requires OpenSSL. Please install OpenSSL and rebuild.";
    return result;
}

inline std::string extract_bearer_token(const std::string& auth_header) {
    return "";
}

inline bool require_admin_auth(const httplib::Request& req, httplib::Response& res, const std::string& jwt_secret) {
    res.status = 503;
    json err;
    err["error"] = "Admin authentication not available - OpenSSL not installed";
    res.set_content(err.dump(2), "application/json");
    return false;
}

#endif // NEXTSEARCH_HAVE_OPENSSL

} // namespace cord19
