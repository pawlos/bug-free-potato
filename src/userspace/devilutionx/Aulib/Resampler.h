// Base class for Aulib resamplers. Our shim does nearest-neighbor
// resampling inside the decoder, so the Resampler type is a tag only.
#pragma once

namespace Aulib {

class Resampler {
public:
    virtual ~Resampler() = default;
};

} // namespace Aulib
