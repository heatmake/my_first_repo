#include "json_helper.h"
#include <fstream>
#include <iostream>

JsonHelper &JsonHelper::GetInstance()
{
    static JsonHelper instance;
    return instance;
}

bool JsonHelper::LoadJson(const std::string &filepath, nlohmann::json &j)
{
    bool success = true;
    std::ifstream in(filepath);

    if (!in.is_open())
    {
        j = nlohmann::json::object();
    }
    else
    {
        try
        {
            in >> j;
        }
        catch (const std::exception &e)
        {
            std::cerr << "[JsonHelper] Failed to parse JSON: " << e.what() << std::endl;
            success = false;
        }
    }

    return success;
}

bool JsonHelper::SaveJson(const std::string &filepath, const nlohmann::json &j)
{
    bool success = true;
    std::ofstream out(filepath);

    if (!out.is_open())
    {
        std::cerr << "[JsonHelper] Failed to open file for writing: " << filepath << std::endl;
        success = false;
    }
    else
    {
        out << j.dump(4);
    }

    return success;
}

bool JsonHelper::WriteString(const std::string &filepath, const std::string &key, const std::string &value)
{
    bool success = false;
    nlohmann::json j;

    if (LoadJson(filepath, j))
    {
        j[key] = value;
        success = SaveJson(filepath, j);
    }

    return success;
}

bool JsonHelper::WriteBool(const std::string &filepath, const std::string &key, bool value)
{
    bool success = false;
    nlohmann::json j;

    if (LoadJson(filepath, j))
    {
        j[key] = value;
        success = SaveJson(filepath, j);
    }

    return success;
}

bool JsonHelper::WriteInt(const std::string &filepath, const std::string &key, int value)
{
    bool success = false;
    nlohmann::json j;

    if (LoadJson(filepath, j))
    {
        j[key] = value;
        success = SaveJson(filepath, j);
    }

    return success;
}

bool JsonHelper::ReadString(const std::string &filepath, const std::string &key, std::string *outValue)
{
    bool success = false;

    if (outValue)
    {
        nlohmann::json j;
        if (LoadJson(filepath, j) && j.contains(key) && j[key].is_string())
        {
            *outValue = j[key].get<std::string>();
            success = true;
        }
    }

    return success;
}

bool JsonHelper::ReadBool(const std::string &filepath, const std::string &key, bool *outValue)
{
    bool success = false;

    if (outValue)
    {
        nlohmann::json j;
        if (LoadJson(filepath, j) && j.contains(key) && j[key].is_boolean())
        {
            *outValue = j[key].get<bool>();
            success = true;
        }
    }

    return success;
}

int JsonHelper::ReadInt(const std::string &filepath, const std::string &key)
{
    int value = 0;
    nlohmann::json j;

    if (LoadJson(filepath, j) && j.contains(key) && j[key].is_number_integer())
    {
        value = j[key].get<int>();
    }

    return value;
}
