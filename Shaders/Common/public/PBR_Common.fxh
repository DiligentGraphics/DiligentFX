#ifndef _PBR_COMMON_FXH_
#define _PBR_COMMON_FXH_

#ifndef PI
#    define PI 3.141592653589793
#endif

float dot_sat(float3 a, float3 b)
{
    return saturate(dot(a, b));
}

float pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float3 pow5(float3 x)
{
    float3 x2 = x * x;
    return x2 * x2 * x;
}

// Returns a random cosine-weighted direction on the hemisphere around z = 1.
void SampleDirectionCosineHemisphere(in  float2 UV,  // Normal random variables
                                     out float3 Dir, // Direction
                                     out float  Prob // Probability of the generated direction
                                     )
{
    Dir.x = cos(2.0 * PI * UV.x) * sqrt(1.0 - UV.y);
    Dir.y = sin(2.0 * PI * UV.x) * sqrt(1.0 - UV.y);
    Dir.z = sqrt(UV.y);

    // Avoid zero probability
    Prob = max(Dir.z, 1e-6) / PI;
}

// Returns a random cosine-weighted direction on the hemisphere around N.
void SampleDirectionCosineHemisphere(in  float3 N,   // Normal
                                     in  float2 UV,  // Normal random variables
                                     out float3 Dir, // Direction
                                     out float  Prob // Probability of the generated direction
                                     )
{
    float3 T = normalize(cross(N, abs(N.y) > 0.5 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0)));
    float3 B = cross(T, N);
    SampleDirectionCosineHemisphere(UV, Dir, Prob);
    Dir = normalize(Dir.x * T + Dir.y * B + Dir.z * N);
}

// Lambertian diffuse
// see https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
float3 LambertianDiffuse(float3 DiffuseColor)
{
    return DiffuseColor / PI;
}

// The following equation models the Fresnel reflectance term of the spec equation (aka F())
// Implementation of fresnel term from "An Inexpensive BRDF Model for Physically based Rendering" by Christophe Schlick
// https://en.wikipedia.org/wiki/Schlick%27s_approximation
//
//      Rf(Theta) = Rf(0) + (1 - Rf(0)) * (1 - cos(Theta))^5
//
//
//           '.       |       .'
//             '.     |Theta.'
//               '.   |   .'
//                 '. | .'
//        ___________'.'___________
//                   '|
//                  ' |
//                 '  |
//                '   |
//               ' Phi|
//
// Note that precise relfectance term is given by the following expression:
//
//      Rf(Theta) = 0.5 * (sin^2(Theta - Phi) / sin^2(Theta + Phi) + tan^2(Theta - Phi) / tan^2(Theta + Phi))
//
#define SCHLICK_REFLECTION(VdotH, Reflectance0, Reflectance90) ((Reflectance0) + ((Reflectance90) - (Reflectance0)) * pow5(clamp(1.0 - (VdotH), 0.0, 1.0)))
float SchlickReflection(float VdotH, float Reflectance0, float Reflectance90)
{
    return SCHLICK_REFLECTION(VdotH, Reflectance0, Reflectance90);
}
float3 SchlickReflection(float VdotH, float3 Reflectance0, float3 Reflectance90)
{
    return SCHLICK_REFLECTION(VdotH, Reflectance0, Reflectance90);
}

float SchlickToF0(float VdotH, float f, float f90)
{
    float x  = clamp(1.0 - VdotH, 0.0, 1.0);
    float x5 = clamp(pow5(x), 0.0, 0.9999);
    return (f - f90 * x5) / (1.0 - x5);
}

float3 SchlickToF0(float VdotH, float3 f, float3 f90)
{
    float x  = clamp(1.0 - VdotH, 0.0, 1.0);
    float x5 = clamp(pow5(x), 0.0, 0.9999);
    return (f - f90 * x5) / (1.0 - x5);
}

// Visibility = G2(v,l,a) / (4 * (n,v) * (n,l))
// see https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf
float SmithGGXVisibilityCorrelated(float NdotL, float NdotV, float AlphaRoughness)
{
    // G1 (masking) is % microfacets visible in 1 direction
    // G2 (shadow-masking) is % microfacets visible in 2 directions
    // If uncorrelated:
    //    G2(NdotL, NdotV) = G1(NdotL) * G1(NdotV)
    //    Less realistic as higher points are more likely visible to both L and V
    //
    // https://ubm-twvideo01.s3.amazonaws.com/o1/vault/gdc2017/Presentations/Hammon_Earl_PBR_Diffuse_Lighting.pdf

    float a2 = AlphaRoughness * AlphaRoughness;

    float GGXV = NdotL * sqrt(max(NdotV * NdotV * (1.0 - a2) + a2, 1e-7));
    float GGXL = NdotV * sqrt(max(NdotL * NdotL * (1.0 - a2) + a2, 1e-7));

    return 0.5 / (GGXV + GGXL);
}

// https://google.github.io/filament/Filament.md.html#materialsystem/anisotropicmodel
float SmithGGXVisibilityCorrelated_Anisotropic(float NdotL,
                                               float NdotV,
                                               float TdotL,
                                               float TdotV,
                                               float BdotL,
                                               float BdotV,
                                               float AlphaRoughnessT,
                                               float AlphaRoughnessB)
{
    float LambdaV = NdotL * max(length(float3(AlphaRoughnessT * TdotV, AlphaRoughnessB * BdotV, NdotV)), 1e-3);
    float LambdaL = NdotV * max(length(float3(AlphaRoughnessT * TdotL, AlphaRoughnessB * BdotL, NdotL)), 1e-3);
    return 0.5 / (LambdaV + LambdaL);
}

// Smith GGX shadow-masking function G2(v,l,a)
float SmithGGXShadowMasking(float NdotL, float NdotV, float AlphaRoughness)
{
    return 4.0 * NdotL * NdotV * SmithGGXVisibilityCorrelated(NdotL, NdotV, AlphaRoughness);
}

// Smith GGX masking function G1
// [1] "Sampling the GGX Distribution of Visible Normals" (2018) by Eric Heitz - eq. (2)
// https://jcgt.org/published/0007/04/01/
float SmithGGXMasking(float NdotV, float AlphaRoughness)
{
    float a2 = AlphaRoughness * AlphaRoughness;

    // In [1], eq. (2) is defined for the tangent-space view direction V:
    //
    //                                        1
    //      G1(V) = -----------------------------------------------------------
    //                                    {      (ax*V.x)^2 + (ay*V.y)^2)  }
    //               1 + 0.5 * ( -1 + sqrt{ 1 + -------------------------- } )
    //                                    {              V.z^2             }
    //
    // Note that [1] uses notation N for the micronormal, but in our case N is the macronormal,
    // while micronormal is H (aka the halfway vector).
    //
    // After multiplying both nominator and denominator by 2*V.z and given that in our
    // case ax = ay = a, we get:
    //
    //                                2 * V.z                                        2 * V.z
    //      G1(V) = ------------------------------------------- =  ----------------------------------------
    //               V.z + sqrt{ V.z^2 + a2 * (V.x^2 + V.y^2) }     V.z + sqrt{ V.z^2 + a2 * (1 - V.z^2) }
    //
    // Since V.z = NdotV, we finally get:

    float Denom = NdotV + sqrt(a2 + (1.0 - a2) * NdotV * NdotV);
    return 2.0 * max(NdotV, 0.0) / max(Denom, 1e-6);
}


// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games, Equation 3.
float NormalDistribution_GGX(float NdotH, float AlphaRoughness)
{
    // "Sampling the GGX Distribution of Visible Normals" (2018) by Eric Heitz - eq. (1)
    // https://jcgt.org/published/0007/04/01/

    // Make sure we reasonably handle AlphaRoughness == 0
    // (which corresponds to delta function)
    AlphaRoughness = max(AlphaRoughness, 1e-3);

    float a2  = AlphaRoughness * AlphaRoughness;
    float nh2 = NdotH * NdotH;
    float f   = nh2 * a2 + (1.0 - nh2);
    return a2 / max(PI * f * f, 1e-9);
}

// https://google.github.io/filament/Filament.md.html#materialsystem/anisotropicmodel
float NormalDistribution_GGX_Anisotropic(float  NdotH,
                                         float  TdotH,
                                         float  BdotH,
                                         float  AlphaRoughnessT,
                                         float  AlphaRoughnessB)
{
    float  a2 = AlphaRoughnessT * AlphaRoughnessB;
    float3 v  = float3(AlphaRoughnessB * TdotH, AlphaRoughnessT * BdotH, a2 * NdotH);
    float  v2 = dot(v, v);
    float  w2 = a2 / max(v2, 1e-6);
    return a2 * w2 * w2 * (1.0 / PI);
}


// Samples a normal from Visible Normal Distribution as described in
// [1] "A Simpler and Exact Sampling Routine for the GGX Distribution of Visible Normals" (2017) by Eric Heitz
//     https://hal.archives-ouvertes.fr/hal-01509746/document
// [2] "Sampling the GGX Distribution of Visible Normals" (2018) by Eric Heitz
//     https://jcgt.org/published/0007/04/01/
//
// Notes:
//      - View direction must be pointing away from the surface.
//      - Returned normal is in tangent space with Z up.
//      - Returned normal should be used to reflect the view direction and obtain
//        the sampling direction.
float3 SmithGGXSampleVisibleNormal(float3 View, // View direction in tangent space
                                   float  ax,   // X roughness
                                   float  ay,   // Y roughness
                                   float  u1,   // Uniform random variable in [0, 1]
                                   float  u2    // Uniform random variable in [0, 1]
)
{
    // Stretch the view vector so we are sampling as if roughness==1
    float3 V = normalize(View * float3(ax, ay, 1.0));

#if 1
    // Technique from [1]
    // Note: while [2] claims to provide a better parameterization, it produces
    //       subjectively noisier images, so we will stick with method from [1].

    // Build an orthonormal basis with V, T1, and T2
    float3 T1 = (V.z < 0.999) ? normalize(cross(V, float3(0.0, 0.0, 1.0))) : float3(1.0, 0.0, 0.0);
    float3 T2 = cross(T1, V);

    // Choose a point on a disk with each half of the disk weighted
    // proportionally to its projection onto direction V
    float a = 1.0 / (1.0 + V.z);
    float r = sqrt(u1);
    float phi = (u2 < a) ? (u2 / a) * PI : PI + (u2 - a) / (1.0 - a) * PI;
    float p1 = r * cos(phi);
    float p2 = r * sin(phi) * ((u2 < a) ? 1.0 : V.z);
#else
    // Technique from [2]
    // Note: [1] uses earlier parameterization that cannot be used with view directions located in the lower
    //       hemisphere (View.z < 0). Though this is not a problem for classic microfacet BSDF models,
    //       [2] provides a better approximation that is not subject to this limitation.

    // Build orthonormal basis (with special case if cross product is zero) (Section 4.1)
    float lensq = dot(V.xy, V.xy);
    float3 T1 = lensq > 0.0 ? float3(-V.y, V.x, 0.0) / sqrt(lensq) : float3(1.0, 0.0, 0.0);
    float3 T2 = cross(V, T1);

    // Sample the projected area (Section 4.2)
    float r   = sqrt(u1);
    float phi = 2.0 * PI * u2;
    float p1 = r * cos(phi);
    float p2 = r * sin(phi);
    float s  = 0.5 * (1.0 + V.z);
    p2 = (1.0 - s) * sqrt(1.0 - p1 * p1) + s * p2;
#endif

    // Calculate the normal in the stretched tangent space
    float3 N = p1 * T1 + p2 * T2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * V;

    // Transform the normal back to the ellipsoid configuration
    return normalize(float3(ax * N.x, ay * N.y, max(0.0, N.z)));
}

// Samples a normal from Visible Normal Distribution as described in
// [1] "Sampling Visible GGX Normals with Spherical Caps" (2023) by Jonathan Dupuy, Anis Benyoub
//     https://arxiv.org/pdf/2306.05044.pdf
float3 SmithGGXSampleVisibleNormalSC(float3 View, // View direction in tangent space
                                     float  ax,   // X roughness
                                     float  ay,   // Y roughness
                                     float  u1,   // Uniform random variable in [0, 1]
                                     float  u2    // Uniform random variable in [0, 1]
)
{
    // Stretch the view vector so we are sampling as if roughness==1
    float3 V = normalize(View * float3(ax, ay, 1.0));

    float Phi = 2.0 * PI * u1;
    float Z = (1.0 - u2) * (1.0 + V.z) - V.z;
    float SinTheta = sqrt(clamp(1.0 - Z * Z, 0.0, 1.0));
    float3 H = float3(SinTheta * cos(Phi), SinTheta * sin(Phi), Z) + V;

    // Transform the normal back to the ellipsoid configuration
    return normalize(float3(ax * H.x, ay * H.y, H.z));
}

// Returns the probability of sampling direction L for the view direction V and normal N
// using the visible normals distribution.
// [1] "Sampling the GGX Distribution of Visible Normals" (2018) by Eric Heitz
// https://jcgt.org/published/0007/04/01/
float SmithGGXSampleDirectionPDF(float3 V, float3 N, float3 L, float AlphaRoughness)
{
    // Micronormal is the halfway vector
    float3 H = normalize(V + L);

    float NdotH = dot(H, N);
    float NdotV = dot(N, V);
    float NdotL = dot(N, L);
    //float VdotH = dot(V, H);
    if (NdotH <= 0.0 || NdotV <= 0.0 || NdotL <= 0.0)
        return 0.0;

    // Note that [1] uses notation N for the micronormal, but in our case N is the macronormal,
    // while micronormal is H (aka the halfway vector).
    float NDF = NormalDistribution_GGX(NdotH, AlphaRoughness); // (1) - D(N)
    float G1  = SmithGGXMasking(NdotV, AlphaRoughness);        // (2) - G1(V)

    float VNDF = G1 /* * VdotH */ * NDF / NdotV; // (3) - Dv(N)
    return  VNDF / (4.0 /* * VdotH */); // (17) - VdotH cancels out
}

struct AngularInfo
{
    float3 N;
    float3 V;
    float3 L;
    float3 H;
    
    float NdotL; // cos angle between normal and light direction
    float NdotV; // cos angle between normal and view direction
    float NdotH; // cos angle between normal and half vector
    float LdotH; // cos angle between light direction and half vector
    float VdotH; // cos angle between view direction and half vector
};

AngularInfo GetAngularInfo(float3 PointToLight, float3 Normal, float3 View)
{
    float3 n = normalize(Normal);       // Outward direction of surface point
    float3 v = normalize(View);         // Direction from surface point to camera
    float3 l = normalize(PointToLight); // Direction from surface point to light
    float3 h = normalize(l + v);        // Direction of the vector between l and v

    AngularInfo info;
    info.N = n;
    info.V = v;
    info.L = l;
    info.H = h;

    info.NdotL = dot_sat(n, l);
    info.NdotV = dot_sat(n, v);
    info.NdotH = dot_sat(n, h);
    info.LdotH = dot_sat(l, h);
    info.VdotH = dot_sat(v, h);

    return info;
}

struct SurfaceReflectanceInfo
{
    float  PerceptualRoughness;
    float3 Reflectance0;
    float3 Reflectance90;
    float3 DiffuseColor;
};

// BRDF with Lambertian diffuse term and Smith-GGX specular term.
void SmithGGX_BRDF(in float3                 PointToLight,
                   in float3                 Normal,
                   in float3                 View,
                   in SurfaceReflectanceInfo SrfInfo,
                   out float3                DiffuseContrib,
                   out float3                SpecContrib,
                   out float                 NdotL)
{
    AngularInfo angularInfo = GetAngularInfo(PointToLight, Normal, View);

    DiffuseContrib = float3(0.0, 0.0, 0.0);
    SpecContrib    = float3(0.0, 0.0, 0.0);
    NdotL          = angularInfo.NdotL;
    // If one of the dot products is larger than zero, no division by zero can happen. Avoids black borders.
    if (angularInfo.NdotL > 0.0 || angularInfo.NdotV > 0.0)
    {
        //           D(h,a) * G2(v,l,a) * F(v,h,f0)
        // f(v,l) = -------------------------------- = D(h,a) * Vis(v,l,a) * F(v,h,f0)
        //               4 * (n,v) * (n,l)
        // where
        //
        // Vis(v,l,a) = G2(v,l,a) / (4 * (n,v) * (n,l))

        // It is not a mistake that AlphaRoughness = PerceptualRoughness ^ 2 and that
        // SmithGGXVisibilityCorrelated and NormalDistribution_GGX then use a2 = AlphaRoughness ^ 2.
        // See eq. 3 in https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
        float  AlphaRoughness = SrfInfo.PerceptualRoughness * SrfInfo.PerceptualRoughness;
        float  D   = NormalDistribution_GGX(angularInfo.NdotH, AlphaRoughness);
        float  Vis = SmithGGXVisibilityCorrelated(angularInfo.NdotL, angularInfo.NdotV, AlphaRoughness);
        float3 F   = SchlickReflection(angularInfo.VdotH, SrfInfo.Reflectance0, SrfInfo.Reflectance90);

        DiffuseContrib = (1.0 - F) * LambertianDiffuse(SrfInfo.DiffuseColor);
        SpecContrib    = F * Vis * D;
    }
}

void SmithGGX_BRDF_Anisotropic(in float3                 PointToLight,
                               in float3                 Normal,
                               in float3                 View,
                               in float3                 Tangent,
                               in float3                 Bitangent,
                               in SurfaceReflectanceInfo SrfInfo,
                               in float                  AlphaRoughnessT,
                               in float                  AlphaRoughnessB,
                               out float3                DiffuseContrib,
                               out float3                SpecContrib,
                               out float                 NdotL)
{
    AngularInfo angularInfo = GetAngularInfo(PointToLight, Normal, View);

    DiffuseContrib = float3(0.0, 0.0, 0.0);
    SpecContrib    = float3(0.0, 0.0, 0.0);
    NdotL          = angularInfo.NdotL;
    if (angularInfo.NdotL > 0.0 || angularInfo.NdotV > 0.0)
    {
        float TdotH = dot(Tangent, angularInfo.H);
        float BdotH = dot(Bitangent, angularInfo.H);
        float TdotL = dot(Tangent, angularInfo.L);
        float TdotV = dot(Tangent, angularInfo.V);
        float BdotL = dot(Bitangent, angularInfo.L);
        float BdotV = dot(Bitangent, angularInfo.V);
        
        float D = NormalDistribution_GGX_Anisotropic(
            angularInfo.NdotH,
            TdotH,
            BdotH,
            AlphaRoughnessT,
            AlphaRoughnessB);

        float Vis = SmithGGXVisibilityCorrelated_Anisotropic(
            angularInfo.NdotL,
            angularInfo.NdotV,
            TdotL,
            TdotV,
            BdotL,
            BdotV,
            AlphaRoughnessT,
            AlphaRoughnessB);
        
        float3 F = SchlickReflection(angularInfo.VdotH, SrfInfo.Reflectance0, SrfInfo.Reflectance90);

        DiffuseContrib = (1.0 - F) * LambertianDiffuse(SrfInfo.DiffuseColor);
        SpecContrib    = F * Vis * D;
    }
}


// Sheen ("Production Friendly Microfacet Sheen BRDF", Estevez and Kulla 2017)

float NormalDistribution_Charlie(float NdotH, float SheenRoughness)
{
    SheenRoughness = max(SheenRoughness, 1e-6); //clamp (0,1]
    float Alpha = SheenRoughness * SheenRoughness;
    float InvA  = 1.0 / Alpha;
    float Cos2h = NdotH * NdotH;
    float Sin2h = max(1.0 - Cos2h, 0.0078125); // 2^(-14/2), so Sin2h^2 > 0 in fp16
    return (2.0 + InvA) * pow(Sin2h, InvA * 0.5) / (2.0 * PI);
}

float LambdaSheenNumericHelper(float x, float AlphaG)
{
    float OneMinusAlphaSq = (1.0 - AlphaG) * (1.0 - AlphaG);
    float a = lerp( 21.5473, 25.32450, OneMinusAlphaSq);
    float b = lerp( 3.82987,  3.32435, OneMinusAlphaSq);
    float c = lerp( 0.19823,  0.16801, OneMinusAlphaSq);
    float d = lerp(-1.97760, -1.27393, OneMinusAlphaSq);
    float e = lerp(-4.32054, -4.85967, OneMinusAlphaSq);
    return a / (1.0 + b * pow(x, c)) + d * x + e;
}

float LambdaSheen(float CosTheta, float AlphaG)
{
    if (abs(CosTheta) < 0.5)
    {
        return exp(LambdaSheenNumericHelper(CosTheta, AlphaG));
    }
    else
    {
        return exp(2.0 * LambdaSheenNumericHelper(0.5, AlphaG) - LambdaSheenNumericHelper(1.0 - CosTheta, AlphaG));
    }
}

float SheenVisibility(float NdotL, float NdotV, float SheenRoughness)
{
    SheenRoughness = max(SheenRoughness, 1e-6); //clamp (0,1]
    float AlphaG = SheenRoughness * SheenRoughness;
    // NOTE: this value is tweaked to work well for grazing angles.
    //       Larger values (e.g. 1e-7) produce dark spots, while
    //       smaller values (e.g. 1e-8) result in bright spots.
    float Epsilon = 5e-8;
    return saturate(1.0 / ((1.0 + LambdaSheen(NdotV, AlphaG) + LambdaSheen(NdotL, AlphaG)) * max(4.0 * NdotV * NdotL, Epsilon)));
}

float3 SheenSpecularBRDF(float3 SheenColor, float SheenRoughness, float NdotL, float NdotV, float NdotH)
{
    float D   = NormalDistribution_Charlie(NdotH, SheenRoughness);
    float Vis = SheenVisibility(NdotL, NdotV, SheenRoughness);
    return SheenColor * D * Vis;
}

#endif // _PBR_COMMON_FXH_
