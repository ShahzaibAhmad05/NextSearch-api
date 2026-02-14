/*
 * backup_data.cpp
 * 
 * Creates a backup zip file containing all cache files, configuration, 
 * third-party dependencies, feedback, and stats.
 * 
 * Usage: backup_data [output_filename]
 * Default: backup_YYYYMMDD_HHMMSS.zip
 */

#include <iostream>
#include <filesystem>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>
#include <cstdlib>

namespace fs = std::filesystem;

// Get current timestamp as string
std::string get_timestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

// Check if file exists
bool file_exists(const fs::path& path) {
    return fs::exists(path) && fs::is_regular_file(path);
}

// Check if directory exists
bool dir_exists(const fs::path& path) {
    return fs::exists(path) && fs::is_directory(path);
}

int main(int argc, char* argv[]) {
    // Determine output filename
    std::string output_filename;
    if (argc > 1) {
        output_filename = argv[1];
    } else {
        output_filename = "backup_" + get_timestamp() + ".zip";
    }

    std::cout << "[backup] Creating backup: " << output_filename << "\n";

    // List of files and directories to backup
    std::vector<std::string> items_to_backup = {
        "search_cache.json",
        "ai_overview_cache.json",
        "ai_summary_cache.json",
        "feedback.json",
        "stats.json",
        ".env",
        "third_party"
    };

    // Check which items exist
    std::vector<std::string> existing_items;
    for (const auto& item : items_to_backup) {
        fs::path path(item);
        if (file_exists(path) || dir_exists(path)) {
            existing_items.push_back(item);
            std::cout << "[backup] Found: " << item << "\n";
        } else {
            std::cout << "[backup] Not found (skipping): " << item << "\n";
        }
    }

    if (existing_items.empty()) {
        std::cerr << "[backup] ERROR: No files found to backup!\n";
        return 1;
    }

    // Build zip command based on platform
    std::string command;
    
#ifdef _WIN32
    // Windows: Use tar command (available in Windows 10+)
    // tar is built into Windows now and supports zip format
    command = "tar -a -cf " + output_filename;
    for (const auto& item : existing_items) {
        command += " \"" + item + "\"";
    }
    
    std::cout << "[backup] Using Windows tar to create zip...\n";
    std::cout << "[backup] Command: " << command << "\n";
    
#else
    // Unix/Linux/Mac: Use zip command
    command = "zip -r " + output_filename;
    for (const auto& item : existing_items) {
        command += " \"" + item + "\"";
    }
    
    std::cout << "[backup] Using zip command...\n";
    std::cout << "[backup] Command: " << command << "\n";
#endif

    // Execute the command
    int result = std::system(command.c_str());
    
    if (result == 0) {
        // Verify the zip file was created
        if (file_exists(output_filename)) {
            auto file_size = fs::file_size(output_filename);
            std::cout << "[backup] SUCCESS! Created " << output_filename 
                      << " (" << file_size << " bytes)\n";
            
            std::cout << "\n[backup] Backup contains:\n";
            for (const auto& item : existing_items) {
                std::cout << "  - " << item << "\n";
            }
            
            return 0;
        } else {
            std::cerr << "[backup] ERROR: Zip command succeeded but file not found!\n";
            return 1;
        }
    } else {
        std::cerr << "[backup] ERROR: Zip command failed with code " << result << "\n";
        std::cerr << "[backup] Make sure tar (Windows) or zip (Unix) is available\n";
        
#ifdef _WIN32
        std::cerr << "[backup] Windows: tar is built-in on Windows 10+\n";
        std::cerr << "[backup] Try running: winget install -e --id 7zip.7zip\n";
#else
        std::cerr << "[backup] Unix/Linux: Install zip with your package manager\n";
        std::cerr << "[backup]   Debian/Ubuntu: sudo apt install zip\n";
        std::cerr << "[backup]   macOS: brew install zip\n";
#endif
        
        return 1;
    }
}
