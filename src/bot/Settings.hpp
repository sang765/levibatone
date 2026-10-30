#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace levibatone::bot {

// Dynamic settings store. Defaults are Baritone's own values
// (baritone.api.Settings / ActionCosts) so path costs behave like the original.
class Settings {
public:
    static Settings &get();

    bool getBool(std::string_view key, bool fallback = false) const;
    int getInt(std::string_view key, int fallback = 0) const;
    double getDouble(std::string_view key, double fallback = 0.0) const;

    // Returns feedback lines ("key = value"); empty on unknown key.
    std::vector<std::string> set(std::string_view key, std::string_view value);
    std::vector<std::string> list() const;
    bool has(std::string_view key) const;

    // "key = value" text file under the mod config dir; every set() persists.
    void loadStore(const std::filesystem::path &path);
    void persist() const;

private:
    Settings();

    std::unordered_map<std::string, std::string> mValues;
    std::filesystem::path mStorePath;
};

}
