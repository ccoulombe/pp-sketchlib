/*
 *
 * database.hpp
 * Header file for database.cpp
 * Interface between sketches and HDF5 store
 * 
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <string>

#include <highfive/H5File.hpp>

#include "reference.hpp"

const size_t DEFAULT_LENGTH = 3000000;

class Database 
{
    public:
        Database(const std::string& filename); // Overwrite or create new H5 file
        Database(HighFive::File& _h5_file); // Open a H5 file
        
        void add_sketch(const Reference& ref); // Write a new sketch to the HDF5
        Reference load_sketch(const std::string& name); // Retrieve a sketch

    private:
        std::string _filename;
        HighFive::File _h5_file;
};

HighFive::File open_h5(const std::string& filename);
