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
#include <cstring>
#include <vector>
#include <string>
#include "robin_hood.h"

#include <highfive/H5File.hpp>

#include "version.h" // Generated by compiler
#include "reference.hpp"

const size_t DEFAULT_LENGTH = 3000000;

class Database
{
public:
  // Open a H5 file
  Database(const std::string &h5_filename, const bool writable = false);

  void add_sketch(const Reference &ref);          // Write a new sketch to the HDF5
  Reference load_sketch(const std::string &name); // Retrieve a sketch
  void save_random(const RandomMC &random);
  RandomMC load_random(const bool use_rc_default);

  std::string version() const { return _version_hash; }
  bool codon_phased() const { return _codon_phased; }
  bool check_version(const Database &db2) const
  {
    if (_codon_phased != db2.codon_phased())
    {
      throw std::runtime_error("One DB uses phased seeds, the other does not");
    }
    return _version_hash == db2.version();
  }
  void flush() { _h5_file.flush(); }

private:
  HighFive::File _h5_file;
  std::string _filename;
  std::string _version_hash;
  bool _codon_phased;
  bool _writable;
};

HighFive::File open_h5(const std::string &filename, const bool write = true);
Database new_db(const std::string &filename, const bool codon_phased);