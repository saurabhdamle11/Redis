#pragma once
#include "types.h"
#include <unordered_map>

void handle_client(int client_fd, const std::unordered_map<std::string, CommandHandler>& commands);
