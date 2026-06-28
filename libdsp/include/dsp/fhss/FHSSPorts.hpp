/**
 * @file FHSSPorts.hpp
 * @brief Canonical accelerator-ready FHSS edge token types.
 */
// SPDX-License-Identifier: MIT

#pragma once

#include "dsp/fhss/FHSSPackets.hpp"
#include "gpu/accel/types/AccelTypes.hpp"

namespace dsp::fhss {

template <typename PacketT>
using FHSSGraphXToken = graph::gpu::accel::ControlToken<PacketT>;

using FHSSSyntheticIqToken = FHSSGraphXToken<FHSSSyntheticIqOutputPacket>;
using FHSSDownconvertedIqToken = FHSSGraphXToken<FHSSDownconvertedIqPacket>;
using FHSSChannelizedIqToken = FHSSGraphXToken<FHSSChannelizedIqPacket>;
using FHSSPerChannelPulseEvidenceToken =
    FHSSGraphXToken<FHSSPerChannelPulseEvidencePacket>;
using FHSSDetectedPulseToken = FHSSGraphXToken<FHSSDetectedPulseEvidencePacket>;
using FHSSPulseCandidateToken =
    FHSSGraphXToken<FHSSPulseCandidateEvidencePacket>;
using FHSSCpsmBranchMetricToken = FHSSGraphXToken<FHSSCpsmBranchMetricPacket>;
using FHSSCpsmSymbolDecisionToken =
    FHSSGraphXToken<FHSSCpsmSymbolDecisionPacket>;
using FHSSDecodedPulseWordToken = FHSSGraphXToken<FHSSDecodedPulseWordPacket>;
using FHSSDecodedPulseWordsToken = FHSSGraphXToken<FHSSDecodedPulseWordsPacket>;
using FHSSAssembledMessageToken = FHSSGraphXToken<FHSSAssembledMessagePacket>;
using FHSSDiagnosticsToken = FHSSGraphXToken<FHSSDiagnosticsPacket>;

} // namespace dsp::fhss
