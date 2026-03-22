#pragma once

// Maps between linear gain [0, 1] and RAOP dB volume [-144.0, 0.0].
// The RAOP spec uses 0.0 dB for full volume and -144.0 dB as the mute floor.
class VolumeMapper
{
public:
    // Maps linear [0, 1] → dB [-144.0, 0.0].
    // linear == 0.0 → exactly -144.0 (mute floor).
    static float LinearToDb(float linear);

    // Maps dB [-144.0, 0.0] → linear [0, 1].
    // db <= -144.0 → 0.0 (mute).
    static float DbToLinear(float db);
};
