#pragma once
#include <functional>
#include <string>
#include <vector>

using Args = std::vector<std::string>;
using CommandHandler = std::function<std::string(const Args&)>;
