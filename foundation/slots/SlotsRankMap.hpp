
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//  File:   SlotsRankMap.hpp
//  Author: Ritchie Brannan
//  Date:   29 Mar 26
//
//  Rank-map entry and vector alias utilities.
//
//  Notes:
//  - RankMapEntry stores producer-defined bidirectional rank/slot mapping.
//  - Unused or invalid mappings are represented with -1 sentinels.

#pragma once

#ifndef SLOTS_RANK_MAP_HPP_INCLUDED
#define SLOTS_RANK_MAP_HPP_INCLUDED

#include <cstdint>      //  std::int32_t

#include "containers/TPodVector.hpp"

namespace slots
{

//  RankMapEntry is a compact bidirectional rank/slot mapping record.
//
//  Interpretation is producer-defined:
//  - rank_to_slot maps a rank-domain index to a slot index
//  - slot_to_rank maps a slot-domain index to a rank index
//
//  Some producers define mappings over the full managed domain.
//  Others define mappings only over an occupied subset.
//
//  Unused or non-participating entries are represented by -1 sentinels.
//  Consuming code must therefore respect the semantic contract of the
//  producing container and must not assume every entry is valid.

struct RankMapEntry
{
    std::int32_t rank_to_slot = -1;
    std::int32_t slot_to_rank = -1;
};

//  Dense rank-map storage.
using RankMap = TPodVector<RankMapEntry>;

}   //  namespace slots

#endif  //  SLOTS_RANK_MAP_HPP_INCLUDED
