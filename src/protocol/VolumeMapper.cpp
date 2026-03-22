#include "protocol/VolumeMapper.h"
#include <cmath>
#include <algorithm>

static constexpr float kMinDb = -144.0f;

float VolumeMapper::LinearToDb(float linear)
{
    if (linear <= 0.0f)
        return kMinDb;
    float db = 20.0f * std::log10(linear);
    return std::max(db, kMinDb);
}

float VolumeMapper::DbToLinear(float db)
{
    if (db <= kMinDb)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}
