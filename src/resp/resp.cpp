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

bool try_parse_resp(std::string& buf, std::vector<std::string>& out) {
    size_t pos = 0;

    // First line: *<count>\r\n
    size_t crlf = buf.find("\r\n", pos);
    if (crlf == std::string::npos) return false;
    if (buf[pos] != '*') return false;

    int count = std::stoi(buf.substr(pos + 1, crlf - pos - 1));
    pos = crlf + 2;

    std::vector<std::string> tokens;
    tokens.reserve(count);

    for (int i = 0; i < count; ++i) {
        // Length line: $<len>\r\n
        crlf = buf.find("\r\n", pos);
        if (crlf == std::string::npos) return false;
        if (buf[pos] != '$') return false;

        int len = std::stoi(buf.substr(pos + 1, crlf - pos - 1));
        pos = crlf + 2;

        // Bulk string data + trailing \r\n
        if (pos + static_cast<size_t>(len) + 2 > buf.size()) return false;

        tokens.push_back(buf.substr(pos, len));
        pos += len + 2;
    }

    buf.erase(0, pos);
    out = std::move(tokens);
    return true;
}
