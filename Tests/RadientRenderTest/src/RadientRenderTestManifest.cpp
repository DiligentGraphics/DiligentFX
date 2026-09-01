/*
 *  Copyright 2026 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include "RadientRenderTestManifest.hpp"

#include "RadientRenderTestOptions.hpp"

#include "Render/RadientPBRRenderer.hpp"
#include "json.hpp"

#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace Diligent
{
namespace Testing
{

namespace
{

using Json = nlohmann::json;

RadientRenderTestManifest Manifest;

bool SetError(std::string& Error, std::string Message)
{
    Error = std::move(Message);
    return false;
}

bool ParseFloat(const Json& Value, float& Result, const std::string& Path, std::string& Error)
{
    if (!Value.is_number())
        return SetError(Error, Path + " must be a number");

    const double Number = Value.get<double>();
    if (!std::isfinite(Number) ||
        Number < -(std::numeric_limits<float>::max)() ||
        Number > (std::numeric_limits<float>::max)())
    {
        return SetError(Error, Path + " must be a finite 32-bit floating-point value");
    }

    Result = static_cast<float>(Number);
    return true;
}

bool ParseFloat3(const Json& Value, float3& Result, const std::string& Path, std::string& Error)
{
    if (!Value.is_array() || Value.size() != 3)
        return SetError(Error, Path + " must be an array of three numbers");

    return ParseFloat(Value[0], Result.x, Path + "[0]", Error) &&
        ParseFloat(Value[1], Result.y, Path + "[1]", Error) &&
        ParseFloat(Value[2], Result.z, Path + "[2]", Error);
}

bool ParseFloat2(const Json& Value, float2& Result, const std::string& Path, std::string& Error)
{
    if (!Value.is_array() || Value.size() != 2)
        return SetError(Error, Path + " must be an array of two numbers");

    return ParseFloat(Value[0], Result.x, Path + "[0]", Error) &&
        ParseFloat(Value[1], Result.y, Path + "[1]", Error);
}

bool ParseUint32(const Json&            Object,
                 const char*            Name,
                 std::optional<Uint32>& Result,
                 const std::string&     Path,
                 std::string&           Error)
{
    const auto It = Object.find(Name);
    if (It == Object.end())
        return true;

    if (!It->is_number_unsigned())
        return SetError(Error, Path + '.' + Name + " must be an unsigned integer");

    const Uint64 Value = It->get<Uint64>();
    if (Value > (std::numeric_limits<Uint32>::max)())
        return SetError(Error, Path + '.' + Name + " is too large");

    Result = static_cast<Uint32>(Value);
    return true;
}

bool ParseStatistics(const Json&                  Object,
                     RadientRenderTestStatistics& Statistics,
                     const std::string&           Path,
                     std::string&                 Error)
{
    if (!Object.is_object())
        return SetError(Error, Path + " must be an object");

    return ParseUint32(Object, "multiDrawIndexed", Statistics.MultiDrawIndexed, Path, Error) &&
        ParseUint32(Object, "mapBufferMax", Statistics.MapBufferMax, Path, Error) &&
        ParseUint32(Object, "updateBufferMax", Statistics.UpdateBufferMax, Path, Error);
}

std::optional<RENDER_DEVICE_TYPE> GetBackendType(const std::string& Name)
{
    if (Name == "d3d11")
        return RENDER_DEVICE_TYPE_D3D11;
    if (Name == "d3d12")
        return RENDER_DEVICE_TYPE_D3D12;
    if (Name == "gl")
        return RENDER_DEVICE_TYPE_GL;
    if (Name == "gles")
        return RENDER_DEVICE_TYPE_GLES;
    if (Name == "vk")
        return RENDER_DEVICE_TYPE_VULKAN;
    if (Name == "mtl")
        return RENDER_DEVICE_TYPE_METAL;
    if (Name == "wgpu")
        return RENDER_DEVICE_TYPE_WEBGPU;
    return {};
}

bool ParseCamera(const Json&              Object,
                 RadientRenderTestCamera& Camera,
                 const std::string&       Path,
                 std::string&             Error)
{
    if (!Object.is_object())
        return SetError(Error, Path + " must be an object");

    const auto EyeIt = Object.find("eye");
    if (EyeIt == Object.end())
        return SetError(Error, Path + ".eye is required");
    if (!ParseFloat3(*EyeIt, Camera.Eye, Path + ".eye", Error))
        return false;

    if (const auto TargetIt = Object.find("target");
        TargetIt != Object.end() && !ParseFloat3(*TargetIt, Camera.Target, Path + ".target", Error))
    {
        return false;
    }

    if (const auto UpIt = Object.find("up");
        UpIt != Object.end() && !ParseFloat3(*UpIt, Camera.Up, Path + ".up", Error))
    {
        return false;
    }

    if (const auto FovIt = Object.find("fov");
        FovIt != Object.end() && !ParseFloat(*FovIt, Camera.Fov, Path + ".fov", Error))
    {
        return false;
    }

    if (const auto ClippingRangeIt = Object.find("clippingRange");
        ClippingRangeIt != Object.end() &&
        !ParseFloat2(*ClippingRangeIt, Camera.ClippingRange, Path + ".clippingRange", Error))
    {
        return false;
    }

    if (Camera.Fov <= 0 || Camera.Fov >= 180)
        return SetError(Error, Path + ".fov must be in the (0, 180) range");
    if (Camera.ClippingRange.x <= 0 || Camera.ClippingRange.y <= Camera.ClippingRange.x)
        return SetError(Error, Path + ".clippingRange must contain positive near and far distances in ascending order");
    if (length(Camera.Up) == 0)
        return SetError(Error, Path + ".up must not be a zero vector");

    const float3 ViewDirection = Camera.Target - Camera.Eye;
    if (length(ViewDirection) == 0)
        return SetError(Error, Path + ".eye and .target must be different");
    if (length(cross(Camera.Up, ViewDirection)) == 0)
        return SetError(Error, Path + ".up must not be parallel to the view direction");

    return true;
}

bool ParseComparison(const Json&                 Object,
                     TestImageComparisonAttribs& Comparison,
                     const std::string&          Path,
                     std::string&                Error)
{
    if (!Object.is_object())
        return SetError(Error, Path + " must be an object");

    if (const auto ChannelErrorIt = Object.find("maxChannelError"); ChannelErrorIt != Object.end())
    {
        if (!ChannelErrorIt->is_number_unsigned())
            return SetError(Error, Path + ".maxChannelError must be an unsigned integer");

        const Uint64 Value = ChannelErrorIt->get<Uint64>();
        if (Value > 255)
            return SetError(Error, Path + ".maxChannelError must be in the [0, 255] range");
        Comparison.MaxChannelError = static_cast<Uint8>(Value);
    }

    if (const auto BadPixelRatioIt = Object.find("maxBadPixelRatio"); BadPixelRatioIt != Object.end())
    {
        if (!ParseFloat(*BadPixelRatioIt, Comparison.MaxBadPixelRatio, Path + ".maxBadPixelRatio", Error))
            return false;
        if (Comparison.MaxBadPixelRatio < 0 || Comparison.MaxBadPixelRatio > 1)
            return SetError(Error, Path + ".maxBadPixelRatio must be in the [0, 1] range");
    }

    return true;
}

bool ParseToneMapping(const Json&             Object,
                      RadientToneMappingDesc& ToneMapping,
                      const std::string&      Path,
                      std::string&            Error)
{
    if (!Object.is_object())
        return SetError(Error, Path + " must be an object");

    if (const auto AverageLogLumIt = Object.find("averageLogLum"); AverageLogLumIt != Object.end())
    {
        float Value = 0.f;
        if (!ParseFloat(*AverageLogLumIt, Value, Path + ".averageLogLum", Error))
            return false;
        if (Value <= 0.f)
            return SetError(Error, Path + ".averageLogLum must be greater than zero");

        ToneMapping.AverageLogLum = Value;
    }

    return true;
}

bool ParseAnimation(const Json&                 Object,
                    RadientRenderTestAnimation& Animation,
                    const std::string&          Path,
                    std::string&                Error)
{
    if (!Object.is_object())
        return SetError(Error, Path + " must be an object");

    const auto NameIt = Object.find("name");
    if (NameIt == Object.end() || !NameIt->is_string())
        return SetError(Error, Path + ".name is required and must be a string");
    Animation.Name = NameIt->get<std::string>();
    if (Animation.Name.empty())
        return SetError(Error, Path + ".name must not be empty");

    const auto TimeIt = Object.find("time");
    if (TimeIt == Object.end())
        return SetError(Error, Path + ".time is required");
    if (!ParseFloat(*TimeIt, Animation.Time, Path + ".time", Error))
        return false;
    if (Animation.Time < 0.f)
        return SetError(Error, Path + ".time must be non-negative");

    return true;
}

bool ParseDebugVisualizations(const Json&                               Value,
                              std::vector<RADIENT_DEBUG_VISUALIZATION>& Result,
                              const std::string&                        Path,
                              std::string&                              Error)
{
    if (!Value.is_array())
        return SetError(Error, Path + " must be an array");

    static const std::unordered_map<std::string, RADIENT_DEBUG_VISUALIZATION> ModesByName = [] {
        std::unordered_map<std::string, RADIENT_DEBUG_VISUALIZATION> Map;
        Map.reserve(RADIENT_DEBUG_VISUALIZATION_COUNT - RADIENT_DEBUG_VISUALIZATION_NONE - 1);
        for (Uint32 ModeIndex = RADIENT_DEBUG_VISUALIZATION_NONE + 1;
             ModeIndex < RADIENT_DEBUG_VISUALIZATION_COUNT;
             ++ModeIndex)
        {
            const auto Mode    = static_cast<RADIENT_DEBUG_VISUALIZATION>(ModeIndex);
            const auto PBRMode = RadientPBRRenderer::GetDebugViewType(Mode);
            Map.emplace(PBR_Renderer::GetDebugViewTypeString(PBRMode), Mode);
        }
        return Map;
    }();

    std::unordered_set<Uint32> Modes;
    Result.reserve(Value.size());
    for (size_t Index = 0; Index < Value.size(); ++Index)
    {
        const Json& Entry = Value[Index];
        if (!Entry.is_string())
            return SetError(Error, Path + '[' + std::to_string(Index) + "] must be a string");

        const std::string Name   = Entry.get<std::string>();
        const auto        ModeIt = ModesByName.find(Name);
        if (ModeIt == ModesByName.end())
            return SetError(Error, Path + " contains unknown debug visualization '" + Name + '\'');

        const RADIENT_DEBUG_VISUALIZATION Mode = ModeIt->second;
        if (!Modes.emplace(static_cast<Uint32>(Mode)).second)
            return SetError(Error, Path + " contains duplicate debug visualization '" + Name + '\'');

        Result.push_back(Mode);
    }

    return true;
}

bool IsValidTestName(const std::string& Name)
{
    if (Name.empty())
        return false;

    for (char Character : Name)
    {
        if (Character != '_' && !std::isalnum(static_cast<unsigned char>(Character)))
            return false;
    }
    return true;
}

bool ParseTestCase(const Json&                     Object,
                   size_t                          TestIndex,
                   const RadientRenderTestOptions& Options,
                   RadientRenderTestCase&          Test,
                   std::string&                    Error)
{
    const std::string Path = "tests[" + std::to_string(TestIndex) + ']';
    if (!Object.is_object())
        return SetError(Error, Path + " must be an object");

    const auto NameIt = Object.find("name");
    if (NameIt == Object.end() || !NameIt->is_string())
        return SetError(Error, Path + ".name is required and must be a string");
    Test.Name = NameIt->get<std::string>();
    if (!IsValidTestName(Test.Name))
        return SetError(Error, Path + ".name must contain only letters, digits, and underscores");

    const auto ModelIt = Object.find("model");
    if (ModelIt == Object.end() || !ModelIt->is_string())
        return SetError(Error, Path + ".model is required and must be a string");
    Test.Model = ModelIt->get<std::string>();
    if (Test.Model.empty())
        return SetError(Error, Path + ".model must not be empty");

    const auto CameraIt = Object.find("camera");
    if (CameraIt == Object.end())
        return SetError(Error, Path + ".camera is required");
    if (!ParseCamera(*CameraIt, Test.Camera, Path + ".camera", Error))
        return false;

    if (const auto ToneMappingIt = Object.find("toneMapping");
        ToneMappingIt != Object.end() &&
        !ParseToneMapping(*ToneMappingIt, Test.ToneMapping, Path + ".toneMapping", Error))
    {
        return false;
    }

    if (const auto DirectionalLightIt = Object.find("directionalLight"); DirectionalLightIt != Object.end())
    {
        if (!DirectionalLightIt->is_boolean())
            return SetError(Error, Path + ".directionalLight must be a boolean");

        Test.DirectionalLight = DirectionalLightIt->get<bool>();
    }

    if (const auto EnableIBLIt = Object.find("enableIBL"); EnableIBLIt != Object.end())
    {
        if (!EnableIBLIt->is_boolean())
            return SetError(Error, Path + ".enableIBL must be a boolean");

        Test.EnableIBL = EnableIBLIt->get<bool>();
    }

    if (const auto AnimationIt = Object.find("animation"); AnimationIt != Object.end())
    {
        RadientRenderTestAnimation Animation;
        if (!ParseAnimation(*AnimationIt, Animation, Path + ".animation", Error))
            return false;
        Test.Animation = std::move(Animation);
    }

    if (const auto DebugVisualizationsIt = Object.find("debugVisualizations");
        DebugVisualizationsIt != Object.end() &&
        !ParseDebugVisualizations(*DebugVisualizationsIt,
                                  Test.DebugVisualizations,
                                  Path + ".debugVisualizations",
                                  Error))
    {
        return false;
    }

    Test.Comparison = Options.Comparison;
    if (const auto ComparisonIt = Object.find("comparison");
        ComparisonIt != Object.end() &&
        !ParseComparison(*ComparisonIt, Test.Comparison, Path + ".comparison", Error))
    {
        return false;
    }

    if (const auto StatisticsIt = Object.find("statistics"); StatisticsIt != Object.end())
    {
        if (!StatisticsIt->is_object())
            return SetError(Error, Path + ".statistics must be an object");

        for (const auto& [BackendName, BackendJson] : StatisticsIt->items())
        {
            const std::optional<RENDER_DEVICE_TYPE> BackendType = GetBackendType(BackendName);
            if (!BackendType)
                return SetError(Error, Path + ".statistics contains unknown backend '" + BackendName + "'");

            RadientRenderTestStatistics BackendStatistics;
            if (!ParseStatistics(BackendJson, BackendStatistics,
                                 Path + ".statistics." + BackendName, Error))
            {
                return false;
            }
            Test.Statistics[static_cast<size_t>(*BackendType)] = std::move(BackendStatistics);
        }
    }

    return true;
}

bool ParseManifest(const Json&                     Root,
                   const RadientRenderTestOptions& Options,
                   RadientRenderTestManifest&      Result,
                   std::string&                    Error)
{
    if (!Root.is_object())
        return SetError(Error, "root must be an object");

    const auto VersionIt = Root.find("version");
    if (VersionIt == Root.end() || !VersionIt->is_number_unsigned())
        return SetError(Error, "version is required and must be an unsigned integer");
    Result.Version = VersionIt->get<Uint32>();
    if (Result.Version != 1)
        return SetError(Error, "unsupported manifest version " + std::to_string(Result.Version));

    const auto TestsIt = Root.find("tests");
    if (TestsIt == Root.end() || !TestsIt->is_array())
        return SetError(Error, "tests is required and must be an array");

    std::unordered_set<std::string> TestNames;
    Result.Tests.resize(TestsIt->size());
    for (size_t TestIndex = 0; TestIndex < TestsIt->size(); ++TestIndex)
    {
        RadientRenderTestCase& Test = Result.Tests[TestIndex];
        if (!ParseTestCase((*TestsIt)[TestIndex], TestIndex, Options, Test, Error))
            return false;
        if (!TestNames.emplace(Test.Name).second)
            return SetError(Error, "duplicate test name '" + Test.Name + "'");
    }

    return true;
}

} // namespace

bool InitializeRadientRenderTestManifest(const RadientRenderTestOptions& Options)
{
    try
    {
        std::ifstream Input{RADIENT_RENDER_TEST_MANIFEST_PATH};
        if (!Input)
        {
            std::cerr << "RadientRenderTest: failed to open manifest '"
                      << RADIENT_RENDER_TEST_MANIFEST_PATH << "'\n";
            return false;
        }

        const Json Root = Json::parse(Input);

        RadientRenderTestManifest ParsedManifest;
        std::string               Error;
        if (!ParseManifest(Root, Options, ParsedManifest, Error))
        {
            std::cerr << "RadientRenderTest: invalid manifest '"
                      << RADIENT_RENDER_TEST_MANIFEST_PATH << "': " << Error << '\n';
            return false;
        }

        Manifest = std::move(ParsedManifest);
        return true;
    }
    catch (const std::exception& Error)
    {
        std::cerr << "RadientRenderTest: failed to parse manifest '"
                  << RADIENT_RENDER_TEST_MANIFEST_PATH << "': " << Error.what() << '\n';
        return false;
    }
}

const RadientRenderTestManifest& GetRadientRenderTestManifest()
{
    return Manifest;
}

} // namespace Testing
} // namespace Diligent
