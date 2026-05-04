#pragma once
#include <string>
#include <vector>

// Used by tests — parses a complete, well-formed RESP string.
std::vector<std::string> parse_resp(const std::string& raw);

// Used by the async server — tries to parse one complete command out of buf.
// On success: removes the consumed bytes from buf, fills out, returns true.
// On incomplete input: leaves buf untouched, returns false.
bool try_parse_resp(std::string& buf, std::vector<std::string>& out);
