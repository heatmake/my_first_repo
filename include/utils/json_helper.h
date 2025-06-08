#ifndef JSON_HELPER_H
#define JSON_HELPER_H

#include <string>
#include <nlohmann/json.hpp>

class JsonHelper
{
public:
    static JsonHelper& GetInstance();

    bool LoadJson(const std::string& filepath, nlohmann::json& j);
    bool SaveJson(const std::string& filepath, const nlohmann::json& j);

    bool WriteString(const std::string& filepath, const std::string& key, const std::string& value);
    bool WriteBool(const std::string& filepath, const std::string& key, bool value);
    bool WriteInt(const std::string& filepath, const std::string& key, int value);

    bool ReadString(const std::string& filepath, const std::string& key, std::string* outValue);
    bool ReadBool(const std::string& filepath, const std::string& key, bool* outValue);
    int ReadInt(const std::string& filepath, const std::string& key);

private:
    JsonHelper() = default;
    ~JsonHelper() = default;

    JsonHelper(const JsonHelper&) = delete;
    JsonHelper& operator=(const JsonHelper&) = delete;
};

#endif // JSON_HELPER_H
