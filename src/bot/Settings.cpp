#include "Settings.hpp"

#include <charconv>
#include <cstdlib>

namespace levibatone::bot {

namespace {

const std::unordered_map<std::string, std::string> kDefaults{
    {"allowBreak", "true"},
    {"allowPlace", "true"},
    {"allowSprint", "true"},
    {"allowDownward", "true"},
    {"maxFallHeightNoWater", "3"},
    {"costHeuristic", "3.563"},
    {"blockPlacementPenalty", "20"},
    {"blockBreakAdditionalPenalty", "2"},
    {"jumpPenalty", "2"},
    {"walkOnWaterOnePenalty", "3"},
    {"mineGoalUpdateInterval", "5"},
    {"movementTimeoutTicks", "100"},
    {"maxNodes", "15000"},
    {"primaryTimeoutMs", "300"},
    {"failureTimeoutMs", "1500"},
    {"mineScanRadius", "24"},
    {"followRadius", "3"},
    {"followRange", "30"},
    {"followUpdateInterval", "10"},
};

bool parseBool(std::string_view value) {
    return value == "true" || value == "1" || value == "on" || value == "yes";
}

std::string boolToString(bool value) { return value ? "true" : "false"; }

} // namespace

Settings &Settings::get() {
    static Settings instance;
    return instance;
}

Settings::Settings() : mValues(kDefaults) {}

bool Settings::getBool(std::string_view key, bool fallback) const {
    const auto it = mValues.find(std::string(key));
    return it == mValues.end() ? fallback : parseBool(it->second);
}

int Settings::getInt(std::string_view key, int fallback) const {
    const auto it = mValues.find(std::string(key));
    if (it == mValues.end()) return fallback;
    int result = fallback;
    const auto [ptr, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), result);
    return ec == std::errc{} ? result : fallback;
}

double Settings::getDouble(std::string_view key, double fallback) const {
    const auto it = mValues.find(std::string(key));
    if (it == mValues.end()) return fallback;
    std::string text(it->second);
    char *end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    return (end && *end == '\0') ? parsed : fallback;
}

bool Settings::has(std::string_view key) const {
    return mValues.find(std::string(key)) != mValues.end();
}

std::vector<std::string> Settings::set(std::string_view key, std::string_view value) {
    std::string keyText(key);
    const auto it = mValues.find(keyText);
    if (it == mValues.end()) {
        return {std::string("unknown setting: ") + keyText};
    }

    const bool isBool = value == "true" || value == "false";
    const bool isNumber = [&] {
        double parsed = 0.0;
        std::string text(value);
        char *end = nullptr;
        parsed = std::strtod(text.c_str(), &end);
        return end && *end == '\0' && end != text.c_str();
    }();
    (void)isNumber;
    if (!isBool && !isNumber) {
        return {std::string("invalid value: ") + std::string(value)};
    }

    it->second = std::string(value);
    return {keyText + " = " + it->second};
}

std::vector<std::string> Settings::list() const {
    std::vector<std::string> lines;
    lines.reserve(mValues.size());
    for (const auto &[key, value] : mValues) {
        lines.push_back(key + " = " + value);
    }
    return lines;
}

}
