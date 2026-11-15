#pragma once
#ifndef CATA_SRC_FP3D_SAVE_PREVIEW_H
#define CATA_SRC_FP3D_SAVE_PREVIEW_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <optional>
#include <string>
#include <utility>
#include "worldfactory.h"
namespace fp3d_save_preview
{
// Auxiliary presentation files never affect the success of a native game save.
void record();
std::optional<std::pair<std::string, save_t>> browse( const std::string &world = "" );
}
#endif
#endif
