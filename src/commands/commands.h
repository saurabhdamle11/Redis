#pragma once
#include "store/store.h"
#include "types.h"
#include <unordered_map>

std::unordered_map<std::string, CommandHandler> build_command_table(Store& store);
