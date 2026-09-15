#ifndef _SHEEN_SAMPLING_FXH_
#define _SHEEN_SAMPLING_FXH_

// Concentrate inverse-CDF knots near both probability endpoints, where the
// grazing Charlie lobe otherwise produces large quantile interpolation errors.
float SheenSamplingProbability(float Coordinate)
{
    float Distance = min(Coordinate, 1.0 - Coordinate);
    float Tail     = 2.0 * Distance * Distance;
    return Coordinate <= 0.5 ? Tail : 1.0 - Tail;
}

float SheenSamplingCoordinate(float Probability)
{
    float Tail = sqrt(0.5 * min(Probability, 1.0 - Probability));
    return Probability <= 0.5 ? Tail : 1.0 - Tail;
}

#endif // _SHEEN_SAMPLING_FXH_
