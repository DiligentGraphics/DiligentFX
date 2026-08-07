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

bool PrepareOutputDirectory(const std::string& Path)
{
    if (!Path.empty() &&
        (FileSystem::IsDirectory(Path.c_str()) ||
         (FileSystem::CreateDirectory(Path.c_str()) && FileSystem::IsDirectory(Path.c_str()))))
    {
        return true;
    }

    std::cerr << "RadientRenderTest: --output must specify a directory that exists or can be created: '"
              << Path << "'\n";
    return false;
}

} // namespace

bool InitializeRadientRenderTestOptions(int argc, const char* const* argv)
{
    CommandLineParser Parser{argc, argv};

    const bool HasModelsDirectory = Parser.Parse("models", Options.ModelsDirectory, false);
    const bool HasGoldenImagesDirectory =
        Parser.Parse("golden_images", Options.GoldenImagesDirectory, false);
    const bool HasOutputDirectory = Parser.Parse("output", Options.OutputDirectory, false);
    const bool HasConfigFile      = Parser.Parse("config", Options.ConfigFile, false);

    if (!HasModelsDirectory || !HasGoldenImagesDirectory || !HasOutputDirectory || !HasConfigFile)
    {
        std::cerr << "Usage: " << argv[0]
                  << " --models=<directory> --golden_images=<directory> --output=<directory> --config=<file> [GPU and gtest options]\n";
        return false;
    }

    bool IsValid = true;
    IsValid &= ValidateDirectory("--models", Options.ModelsDirectory);
    IsValid &= ValidateDirectory("--golden_images", Options.GoldenImagesDirectory);
    IsValid &= PrepareOutputDirectory(Options.OutputDirectory);

    if (Options.ConfigFile.empty() || !FileSystem::FileExists(Options.ConfigFile.c_str()))
    {
        std::cerr << "RadientRenderTest: --config must specify an existing file: '"
                  << Options.ConfigFile << "'\n";
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
