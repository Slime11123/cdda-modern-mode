#pragma once
#ifndef CATA_SRC_FP3D_SHADER_ARCHIVE_H
#define CATA_SRC_FP3D_SHADER_ARCHIVE_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <filesystem>
#include <string>
namespace fp3d_shader_archive
{
// ZIP32 only: deflate/stored and CRC-verified, no extraction to disk.
// Only exact internal paths or one clean outer folder are accepted.
bool read_post( const std::filesystem::path &file, std::string &source );
bool read_mc_entry( const std::filesystem::path &file,
                    const std::string &entry, std::string &source );
} // namespace fp3d_shader_archive
#endif
#endif
