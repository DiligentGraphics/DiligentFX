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

#include "NativeCapture.h"

#include "Image.h"
#include "ImageTools.h"
#include "ScreenCapture.hpp"

#include <cstdio>
#include <memory>
#include <vector>

namespace Diligent
{
namespace
{

struct CaptureState
{
    RefCntAutoPtr<IDeviceContext>  Context;
    std::unique_ptr<ScreenCapture> Capture;
};

std::unique_ptr<CaptureState> State;

bool WritePNG(const char* Path, const Uint8* Pixels, Uint32 Width, Uint32 Height, Uint32 Stride)
{
    Image::EncodeInfo Info;
    Info.Width      = Width;
    Info.Height     = Height;
    Info.TexFormat  = TEX_FORMAT_RGBA8_UNORM;
    Info.pData      = Pixels;
    Info.Stride     = Stride;
    Info.FileFormat = IMAGE_FILE_FORMAT_PNG;
    RefCntAutoPtr<IDataBlob> Encoded;
    Image::Encode(Info, &Encoded);
    if (!Encoded)
        return false;

    std::FILE* File = std::fopen(Path, "wb");
    if (!File)
        return false;
    const bool Written = std::fwrite(Encoded->GetConstDataPtr(), 1, Encoded->GetSize(), File) == Encoded->GetSize();
    return std::fclose(File) == 0 && Written;
}

} // namespace

RADIENT_STATUS BeginBrowserImageCapture(IRadientEngine* pEngine)
{
    if (!pEngine)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    if (State && State->Capture->GetNumPendingCaptures() != 0)
        return RADIENT_STATUS_INVALID_OPERATION;
    try
    {
        // The engine is used only to access the native device/context/swap chain.
        RefCntAutoPtr<IRadientBackend> Backend;
        const RADIENT_STATUS           Status = pEngine->GetBackend(&Backend);
        if (RADIENT_FAILED(Status))
            return Status;
        if (!State)
        {
            State          = std::make_unique<CaptureState>();
            State->Context = Backend->GetNativeImmediateContext();
            State->Capture = std::make_unique<ScreenCapture>(Backend->GetNativeDevice());
        }
        // Queue the copy before Present releases the current canvas texture.
        State->Capture->Capture(Backend->GetNativeSwapChain(), State->Context, 0);
        return RADIENT_STATUS_OK;
    }
    catch (...)
    {
        State.reset();
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS SaveBrowserImageCapture(const char* Path)
{
    if (!State || !Path)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    ScreenCapture::CaptureInfo Capture = State->Capture->GetCapture();
    if (!Capture)
        return RADIENT_STATUS_PENDING;

    MappedTextureSubresource Mapped;
    State->Context->MapTextureSubresource(Capture.pTexture, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, Mapped);
    if (!Mapped.pData)
        return RADIENT_STATUS_FAILED;

    bool Written = false;
    try
    {
        const TextureDesc& Desc = Capture.pTexture->GetDesc();
        // Convert BGRA surfaces when needed, and remove GPU row padding. No
        // canvas/compositor color conversion is involved in the captured pixels.
        const std::vector<Uint8> Pixels = Image::ConvertImageData(
            Desc.Width, Desc.Height, static_cast<const Uint8*>(Mapped.pData),
            static_cast<Uint32>(Mapped.Stride), Desc.Format, TEX_FORMAT_RGBA8_UNORM, true, false);
        Written = WritePNG(Path, Pixels.data(), Desc.Width, Desc.Height, Desc.Width * 4);
    }
    catch (...)
    {
        Written = false;
    }
    State->Context->UnmapTextureSubresource(Capture.pTexture, 0, 0);
    State->Capture->RecycleStagingTexture(std::move(Capture.pTexture));
    return Written ? RADIENT_STATUS_OK : RADIENT_STATUS_FAILED;
}

void ReleaseBrowserImageCapture()
{
    State.reset();
}

RADIENT_STATUS CompareBrowserImages(const char* ActualPath, const char* ReferencePath, const char* DifferencePath, Uint32 MaxChannelError, float MaxBadPixelRatio, BrowserImageComparison& Result)
{
    Result = {};
    if (!ActualPath || !ReferencePath || !DifferencePath || MaxChannelError > 255 ||
        !(MaxBadPixelRatio >= 0 && MaxBadPixelRatio <= 1))
        return RADIENT_STATUS_INVALID_ARGUMENT;
    try
    {
        RefCntAutoPtr<Image> Actual;
        CreateImageFromFile(ActualPath, &Actual);
        if (!Actual)
            return RADIENT_STATUS_FAILED;
        // A missing reference is a comparison failure reported by JavaScript,
        // not a decoder error; the actual image remains available for review.
        std::FILE* File = std::fopen(ReferencePath, "rb");
        if (!File)
            return RADIENT_STATUS_OK;
        std::fclose(File);
        Result.HasReference = true;
        RefCntAutoPtr<Image> Reference;
        CreateImageFromFile(ReferencePath, &Reference);
        if (!Reference)
            return RADIENT_STATUS_INVALID_DATA;
        const ImageDesc& A    = Actual->GetDesc();
        const ImageDesc& B    = Reference->GetDesc();
        Result.SameDimensions = A.Width == B.Width && A.Height == B.Height;
        if (!Result.SameDimensions)
            return RADIENT_STATUS_OK;

        std::vector<Uint8>            Difference(size_t{A.Width} * A.Height * 4);
        ComputeImageDifferenceAttribs Attribs;
        Attribs.Width           = A.Width;
        Attribs.Height          = A.Height;
        Attribs.pImage1         = Actual->GetData()->GetConstDataPtr();
        Attribs.NumChannels1    = A.NumComponents;
        Attribs.Stride1         = A.RowStride;
        Attribs.pImage2         = Reference->GetData()->GetConstDataPtr();
        Attribs.NumChannels2    = B.NumComponents;
        Attribs.Stride2         = B.RowStride;
        Attribs.Threshold       = MaxChannelError;
        Attribs.pDiffImage      = Difference.data();
        Attribs.DiffStride      = A.Width * 4;
        Attribs.NumDiffChannels = 4;
        Attribs.Scale           = 16.f;
        ImageDiffInfo Diff;
        ComputeImageDifference(Attribs, Diff);
        Result.DifferentPixels = Diff.NumDiffPixels;
        Result.BadPixels       = Diff.NumDiffPixelsAboveThreshold;
        Result.MaxDifference   = Diff.MaxDiff;
        Result.Passed          = static_cast<double>(Diff.NumDiffPixelsAboveThreshold) <= static_cast<double>(A.Width) * A.Height * MaxBadPixelRatio;
        if (Diff.NumDiffPixels != 0 && !WritePNG(DifferencePath, Difference.data(), A.Width, A.Height, A.Width * 4))
            return RADIENT_STATUS_FAILED;
        return RADIENT_STATUS_OK;
    }
    catch (...)
    {
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace Diligent
