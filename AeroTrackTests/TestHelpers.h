#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <windows.h>

namespace AeroTrackTests
{
    struct TempFile
    {
        std::string path;

        explicit TempFile(const char* name)
        {
            char dir[MAX_PATH] = { 0 };
            (void)::GetTempPathA(MAX_PATH, dir);
            path = std::string(dir) + name;
        }

        ~TempFile()
        {
            if (!path.empty()) { (void)::DeleteFileA(path.c_str()); }
        }

        const std::string& Path() const { return path; }

        std::string ReadAll() const
        {
            std::ifstream in(path);
            if (!in.is_open()) { return {}; }
            std::ostringstream buf;
            buf << in.rdbuf();
            return buf.str();
        }

        bool Contains(const std::string& sub) const
        {
            return ReadAll().find(sub) != std::string::npos;
        }

        std::vector<std::string> Lines() const
        {
            std::vector<std::string> result;
            std::ifstream in(path);
            if (!in.is_open()) { return result; }
            std::string line;
            while (std::getline(in, line))
            {
                if (!line.empty()) { result.push_back(line); }
            }
            return result;
        }
    };
}
