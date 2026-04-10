// Placeholder SDL resampler type for the Aulib shim.
#pragma once

#include <Aulib/Resampler.h>

namespace Aulib {

class ResamplerSdl final : public Resampler {
public:
    ResamplerSdl() = default;
};

} // namespace Aulib
