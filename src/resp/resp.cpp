#include "resp.h"
#include <sstream>

std::vector<std::string> parse_resp(const std::string& raw) {
    std::vector<std::string> tokens;
    std::istringstream stream(raw);
    std::string line;

    std::getline(stream, line);
    if (line.empty() || line[0] != '*') return tokens;

    int num_elements = std::stoi(line.substr(1));

    for (int i = 0; i < num_elements; ++i) {
        std::getline(stream, line);
        if (line.empty() || line[0] != '$') break;

        std::getline(stream, line);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        tokens.push_back(line);
    }
    return tokens;
}
