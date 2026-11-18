/* A PlayStation disc image (.cue + .bin of raw 2352-byte sectors, or a plain 2048-byte .iso) read without any extraction
   tool: its sectors, its ISO 9660 file list and files by path. What tools/discfs.py did. */
#pragma once
#include <cstdio>
#include <map>
#include <string>
#include <utility>

#include "bytes.h"

class Disc {
  public:
    ~Disc();
    /* false with ERR set when the image can't be opened (a .cue names its .bin) */
    bool open(const std::string &utf8_path, std::string &err);
    Bytes sector(long n);
    Bytes read(long lba, long size);
    /* every file, by its first sector: (path, size) */
    const std::map<uint32_t, std::pair<std::string, uint32_t>> &files();
    bool find(const std::string &path, uint32_t &lba, uint32_t &size);

  private:
    FILE *f_ = nullptr;
    int stride_ = 2048, skip_ = 0;
    bool listed_ = false;
    std::map<uint32_t, std::pair<std::string, uint32_t>> files_;
};
