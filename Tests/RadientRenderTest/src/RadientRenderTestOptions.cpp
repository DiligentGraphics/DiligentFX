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

#include "RadientRenderTestOptions.hpp"

#include "CommandLineParser.hpp"
#include "FileSystem.hpp"

#include <cmath>
#include <iostream>

namespace Diligent
{
namespace Testing
{

namespace
{

RadientRenderTestOptions Options;

bool ValidateDirectory(const char* OptionName, const std::string& Path)
{
    if (!Path.empty() && FileSystem::IsDirectory(Path.c_str()))
        return true;

    std::cerr << "RadientRenderTest: " << OptionName
              << " must specify an existing directory: '" << Path << "'\n";
    return false;
}

bool PrepareDirectory(const char* DirectoryName, const std::string& Path)
{
    if (!Path.empty() &&
        (FileSystem::IsDirectory(Path.c_str()) ||
         (FileSystem::CreateDirectory(Path.c_str()) && FileSystem::IsDirectory(Path.c_str()))))
    {
        return true;
    }

    std::cerr << "RadientRenderTest: " << DirectoryName
              << " directory does not exist and could not be created: '" << Path << "'\n";
    return false;
}

} // namespace

bool InitializeRadientRenderTestOptions(int argc, const char* const* argv)
{
    CommandLineParser Parser{argc, argv};

    const bool HasModelsDirectory = Parser.Parse("models", Options.ModelsDirectory, false);
    const bool HasAssetsDirectory = Parser.Parse("assets", Options.AssetsDirectory, false);

    if (!HasModelsDirectory || !HasAssetsDirectory)
    {
        std::cerr << "Usage: " << argv[0]
                  << " --models=<directory> --assets=<directory> "
                     "[--update_golden_images] [--width=512] [--height=512] [--max_channel_error=0] "
                     "[--max_bad_pixel_ratio=0] [GPU and gtest options]\n";
        return false;
    }

    Options.GoldenImagesDirectory           = FileSystem::JoinPath(Options.AssetsDirectory, "GoldenImages");
    Options.GoldenImageDifferencesDirectory = FileSystem::JoinPath(Options.AssetsDirectory, "GoldenImagesDifferences");

    Parser.Parse("update_golden_images", Options.UpdateGoldenImages, false);
    Parser.Parse("width", Options.Width, false);
    Parser.Parse("height", Options.Height, false);

    Uint32 MaxChannelError = Options.Comparison.MaxChannelError;
    Parser.Parse("max_channel_error", MaxChannelError, false);
    Parser.Parse("max_bad_pixel_ratio", Options.Comparison.MaxBadPixelRatio, false);

    bool IsValid = true;
    IsValid &= ValidateDirectory("--models", Options.ModelsDirectory);
    IsValid &= ValidateDirectory("--assets", Options.AssetsDirectory);
    IsValid &= ValidateDirectory("GoldenImages asset directory", Options.GoldenImagesDirectory);
    IsValid &= PrepareDirectory("GoldenImagesDifferences", Options.GoldenImageDifferencesDirectory);

    if (Options.Width == 0 || Options.Height == 0)
    {
        std::cerr << "RadientRenderTest: --width and --height must be greater than zero\n";
        IsValid = false;
    }

    if (MaxChannelError > 255)
    {
        std::cerr << "RadientRenderTest: --max_channel_error must be in the [0, 255] range\n";
        IsValid = false;
    }
    else
    {
        Options.Comparison.MaxChannelError = static_cast<Uint8>(MaxChannelError);
    }

    if (!std::isfinite(Options.Comparison.MaxBadPixelRatio) ||
        Options.Comparison.MaxBadPixelRatio < 0 ||
        Options.Comparison.MaxBadPixelRatio > 1)
    {
        std::cerr << "RadientRenderTest: --max_bad_pixel_ratio must be finite and in the [0, 1] range\n";
        IsValid = false;
    }

    return IsValid;
}

const RadientRenderTestOptions& GetRadientRenderTestOptions()
{
    return Options;
}

} // namespace Testing
} // namespace Diligent
