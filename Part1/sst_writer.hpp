#pragma once
#include "sst_types.hpp"
#include <string>
#include <filesystem>

class SstFileWriter {
    int fd{-1};
    std::string filepath;
    std::string temp_filepath;
};

