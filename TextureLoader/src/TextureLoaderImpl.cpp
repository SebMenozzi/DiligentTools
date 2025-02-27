/*
 *  Copyright 2019-2022 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
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

#include "pch.h"
#include <algorithm>
#include <limits>
#include <math.h>
#include <vector>

#include "TextureLoaderImpl.hpp"
#include "GraphicsAccessories.hpp"
#include "GraphicsUtilities.h"
#include "PNGCodec.h"
#include "JPEGCodec.h"
#include "ColorConversion.h"
#include "Image.h"
#include "FileWrapper.hpp"
#include "DataBlobImpl.hpp"
#include "Align.hpp"

extern "C"
{
    Diligent::DECODE_PNG_RESULT Diligent_DecodePng(Diligent::IDataBlob* pSrcPngBits,
                                                   Diligent::IDataBlob* pDstPixels,
                                                   Diligent::ImageDesc* pDstImgDesc);

    Diligent::ENCODE_PNG_RESULT Diligent_EncodePng(const Diligent::Uint8* pSrcPixels,
                                                   Diligent::Uint32       Width,
                                                   Diligent::Uint32       Height,
                                                   Diligent::Uint32       StrideInBytes,
                                                   int                    PngColorType,
                                                   Diligent::IDataBlob*   pDstPngBits);

    Diligent::DECODE_JPEG_RESULT Diligent_DecodeJpeg(Diligent::IDataBlob* pSrcJpegBits,
                                                     Diligent::IDataBlob* pDstPixels,
                                                     Diligent::ImageDesc* pDstImgDesc);

    Diligent::ENCODE_JPEG_RESULT Diligent_EncodeJpeg(Diligent::Uint8*     pSrcRGBData,
                                                     Diligent::Uint32     Width,
                                                     Diligent::Uint32     Height,
                                                     int                  quality,
                                                     Diligent::IDataBlob* pDstJpegBits);

    Diligent::DECODE_JPEG_RESULT Diligent_LoadSGI(Diligent::IDataBlob* pSrcJpegBits,
                                                  Diligent::IDataBlob* pDstPixels,
                                                  Diligent::ImageDesc* pDstImgDesc);
}

namespace Diligent
{

template <typename ChannelType>
void ModifyComponentCount(const void* pSrcData,
                          Uint32      SrcStride,
                          Uint32      SrcCompCount,
                          void*       pDstData,
                          Uint32      DstStride,
                          Uint32      Width,
                          Uint32      Height,
                          Uint32      DstCompCount)
{
    auto CompToCopy = std::min(SrcCompCount, DstCompCount);
    for (size_t row = 0; row < size_t{Height}; ++row)
    {
        for (size_t col = 0; col < size_t{Width}; ++col)
        {
            // clang-format off
            auto*       pDst = reinterpret_cast<      ChannelType*>((reinterpret_cast<      Uint8*>(pDstData) + size_t{DstStride} * row)) + col * DstCompCount;
            const auto* pSrc = reinterpret_cast<const ChannelType*>((reinterpret_cast<const Uint8*>(pSrcData) + size_t{SrcStride} * row)) + col * SrcCompCount;
            // clang-format on

            for (size_t c = 0; c < CompToCopy; ++c)
                pDst[c] = pSrc[c];

            for (size_t c = CompToCopy; c < DstCompCount; ++c)
            {
                pDst[c] = c < 3 ?
                    (SrcCompCount == 1 ? pSrc[0] : 0) :      // For single-channel source textures, propagate r to other channels
                    std::numeric_limits<ChannelType>::max(); // Use 1.0 as default value for alpha
            }
        }
    }
}

DECODE_PNG_RESULT DecodePng(IDataBlob* pSrcPngBits,
                            IDataBlob* pDstPixels,
                            ImageDesc* pDstImgDesc)
{
    return Diligent_DecodePng(pSrcPngBits, pDstPixels, pDstImgDesc);
}

ENCODE_PNG_RESULT EncodePng(const Uint8* pSrcPixels,
                            Uint32       Width,
                            Uint32       Height,
                            Uint32       StrideInBytes,
                            int          PngColorType,
                            IDataBlob*   pDstPngBits)
{
    return Diligent_EncodePng(pSrcPixels, Width, Height, StrideInBytes, PngColorType, pDstPngBits);
}


DECODE_JPEG_RESULT DecodeJpeg(IDataBlob* pSrcJpegBits,
                              IDataBlob* pDstPixels,
                              ImageDesc* pDstImgDesc)
{
    return Diligent_DecodeJpeg(pSrcJpegBits, pDstPixels, pDstImgDesc);
}

ENCODE_JPEG_RESULT EncodeJpeg(Uint8*     pSrcRGBPixels,
                              Uint32     Width,
                              Uint32     Height,
                              int        quality,
                              IDataBlob* pDstJpegBits)
{
    return Diligent_EncodeJpeg(pSrcRGBPixels, Width, Height, quality, pDstJpegBits);
}

static TextureDesc TexDescFromTexLoadInfo(const TextureLoadInfo& TexLoadInfo, const std::string& Name)
{
    TextureDesc TexDesc;
    TexDesc.Name           = Name.c_str();
    TexDesc.Format         = TexLoadInfo.Format;
    TexDesc.Usage          = TexLoadInfo.Usage;
    TexDesc.BindFlags      = TexLoadInfo.BindFlags;
    TexDesc.CPUAccessFlags = TexLoadInfo.CPUAccessFlags;
    return TexDesc;
}

TextureLoaderImpl::TextureLoaderImpl(IReferenceCounters*        pRefCounters,
                                     const TextureLoadInfo&     TexLoadInfo,
                                     const Uint8*               pData,
                                     size_t                     DataSize,
                                     RefCntAutoPtr<IDataBlob>&& pDataBlob) :
    TBase{pRefCounters},
    m_pDataBlob{std::move(pDataBlob)},
    m_Name{TexLoadInfo.Name != nullptr ? TexLoadInfo.Name : ""},
    m_TexDesc{TexDescFromTexLoadInfo(TexLoadInfo, m_Name)}
{
    const auto ImgFileFormat = Image::GetFileFormat(pData, DataSize);
    if (ImgFileFormat == IMAGE_FILE_FORMAT_UNKNOWN)
    {
        LOG_ERROR_AND_THROW("Unable to derive image format.");
    }

    if (ImgFileFormat == IMAGE_FILE_FORMAT_PNG ||
        ImgFileFormat == IMAGE_FILE_FORMAT_JPEG ||
        ImgFileFormat == IMAGE_FILE_FORMAT_TIFF ||
        ImgFileFormat == IMAGE_FILE_FORMAT_SGI)
    {
        ImageLoadInfo ImgLoadInfo;
        ImgLoadInfo.Format = ImgFileFormat;
        if (!m_pDataBlob)
        {
            m_pDataBlob = MakeNewRCObj<DataBlobImpl>()(DataSize, pData);
        }
        Image::CreateFromDataBlob(m_pDataBlob, ImgLoadInfo, &m_pImage);
        LoadFromImage(TexLoadInfo);
        m_pDataBlob.Release();
    }
    else
    {
        if (ImgFileFormat == IMAGE_FILE_FORMAT_DDS)
        {
            LoadFromDDS(TexLoadInfo, pData, DataSize);
        }
        else if (ImgFileFormat == IMAGE_FILE_FORMAT_KTX)
        {
            LoadFromKTX(TexLoadInfo, pData, DataSize);
        }
    }

    if (TexLoadInfo.IsSRGB)
    {
        m_TexDesc.Format = TexFormatToSRGB(m_TexDesc.Format);
    }
}

TextureLoaderImpl::TextureLoaderImpl(IReferenceCounters*    pRefCounters,
                                     const TextureLoadInfo& TexLoadInfo,
                                     Image*                 pImage) :
    TBase{pRefCounters},
    m_pImage{pImage},
    m_Name{TexLoadInfo.Name != nullptr ? TexLoadInfo.Name : ""},
    m_TexDesc{TexDescFromTexLoadInfo(TexLoadInfo, m_Name)}
{
    LoadFromImage(TexLoadInfo);
}

void TextureLoaderImpl::CreateTexture(IRenderDevice* pDevice,
                                      ITexture**     ppTexture)
{
    TextureData InitData{m_SubResources.data(), static_cast<Uint32>(m_SubResources.size())};
    pDevice->CreateTexture(m_TexDesc, &InitData, ppTexture);
}


void TextureLoaderImpl::LoadFromImage(const TextureLoadInfo& TexLoadInfo)
{
    VERIFY_EXPR(m_pImage);

    const auto& ImgDesc      = m_pImage->GetDesc();
    const auto  ChannelDepth = GetValueSize(ImgDesc.ComponentType) * 8;

    m_TexDesc.Type      = RESOURCE_DIM_TEX_2D;
    m_TexDesc.Width     = ImgDesc.Width;
    m_TexDesc.Height    = ImgDesc.Height;
    m_TexDesc.MipLevels = ComputeMipLevelsCount(m_TexDesc.Width, m_TexDesc.Height);
    if (TexLoadInfo.MipLevels > 0)
        m_TexDesc.MipLevels = std::min(m_TexDesc.MipLevels, TexLoadInfo.MipLevels);

    Uint32 NumComponents = 0;
    if (m_TexDesc.Format == TEX_FORMAT_UNKNOWN)
    {
        NumComponents = ImgDesc.NumComponents == 3 ? 4 : ImgDesc.NumComponents;
        if (ChannelDepth == 8)
        {
            switch (NumComponents)
            {
                case 1: m_TexDesc.Format = TEX_FORMAT_R8_UNORM; break;
                case 2: m_TexDesc.Format = TEX_FORMAT_RG8_UNORM; break;
                case 4: m_TexDesc.Format = TexLoadInfo.IsSRGB ? TEX_FORMAT_RGBA8_UNORM_SRGB : TEX_FORMAT_RGBA8_UNORM; break;
                default: LOG_ERROR_AND_THROW("Unexpected number of color channels (", ImgDesc.NumComponents, ")");
            }
        }
        else if (ChannelDepth == 16)
        {
            switch (NumComponents)
            {
                case 1: m_TexDesc.Format = TEX_FORMAT_R16_UNORM; break;
                case 2: m_TexDesc.Format = TEX_FORMAT_RG16_UNORM; break;
                case 4: m_TexDesc.Format = TEX_FORMAT_RGBA16_UNORM; break;
                default: LOG_ERROR_AND_THROW("Unexpected number of color channels (", ImgDesc.NumComponents, ")");
            }
        }
        else
            LOG_ERROR_AND_THROW("Unsupported color channel depth (", ChannelDepth, ")");
    }
    else
    {
        const auto& TexFmtDesc = GetTextureFormatAttribs(m_TexDesc.Format);

        NumComponents = TexFmtDesc.NumComponents;
        if (TexFmtDesc.ComponentSize != ChannelDepth / 8)
            LOG_ERROR_AND_THROW("Image channel size ", ChannelDepth, " is not compatible with texture format ", TexFmtDesc.Name);
    }

    m_SubResources.resize(m_TexDesc.MipLevels);
    m_Mips.resize(m_TexDesc.MipLevels);

    if (ImgDesc.NumComponents != NumComponents)
    {
        auto DstStride = ImgDesc.Width * NumComponents * ChannelDepth / 8;
        DstStride      = AlignUp(DstStride, Uint32{4});
        m_Mips[0].resize(size_t{DstStride} * size_t{ImgDesc.Height});
        m_SubResources[0].pData  = m_Mips[0].data();
        m_SubResources[0].Stride = DstStride;
        if (ChannelDepth == 8)
        {
            ModifyComponentCount<Uint8>(m_pImage->GetData()->GetDataPtr(), ImgDesc.RowStride, ImgDesc.NumComponents,
                                        m_Mips[0].data(), DstStride,
                                        ImgDesc.Width, ImgDesc.Height, NumComponents);
        }
        else if (ChannelDepth == 16)
        {
            ModifyComponentCount<Uint16>(m_pImage->GetData()->GetDataPtr(), ImgDesc.RowStride, ImgDesc.NumComponents,
                                         m_Mips[0].data(), DstStride,
                                         ImgDesc.Width, ImgDesc.Height, NumComponents);
        }
    }
    else
    {
        m_SubResources[0].pData  = m_pImage->GetData()->GetDataPtr();
        m_SubResources[0].Stride = ImgDesc.RowStride;
    }

    for (Uint32 m = 1; m < m_TexDesc.MipLevels; ++m)
    {
        auto MipLevelProps = GetMipLevelProperties(m_TexDesc, m);
        m_Mips[m].resize(StaticCast<size_t>(MipLevelProps.MipSize));
        m_SubResources[m].pData  = m_Mips[m].data();
        m_SubResources[m].Stride = MipLevelProps.RowSize;

        if (TexLoadInfo.GenerateMips)
        {
            auto FinerMipProps = GetMipLevelProperties(m_TexDesc, m - 1);
            if (TexLoadInfo.GenerateMips)
            {
                ComputeMipLevelAttribs Attribs;
                Attribs.Format          = m_TexDesc.Format;
                Attribs.FineMipWidth    = FinerMipProps.LogicalWidth;
                Attribs.FineMipHeight   = FinerMipProps.LogicalHeight;
                Attribs.pFineMipData    = m_SubResources[m - 1].pData;
                Attribs.FineMipStride   = StaticCast<size_t>(m_SubResources[m - 1].Stride);
                Attribs.pCoarseMipData  = m_Mips[m].data();
                Attribs.CoarseMipStride = StaticCast<size_t>(m_SubResources[m].Stride);
                Attribs.AlphaCutoff     = TexLoadInfo.AlphaCutoff;
                static_assert(MIP_FILTER_TYPE_DEFAULT == static_cast<MIP_FILTER_TYPE>(TEXTURE_LOAD_MIP_FILTER_DEFAULT), "Inconsistent enum values");
                static_assert(MIP_FILTER_TYPE_BOX_AVERAGE == static_cast<MIP_FILTER_TYPE>(TEXTURE_LOAD_MIP_FILTER_BOX_AVERAGE), "Inconsistent enum values");
                static_assert(MIP_FILTER_TYPE_MOST_FREQUENT == static_cast<MIP_FILTER_TYPE>(TEXTURE_LOAD_MIP_FILTER_MOST_FREQUENT), "Inconsistent enum values");
                Attribs.FilterType = static_cast<MIP_FILTER_TYPE>(TexLoadInfo.MipFilter);
                ComputeMipLevel(Attribs);
            }
        }
    }
}


void CreateTextureLoaderFromFile(const char*            FilePath,
                                 IMAGE_FILE_FORMAT      FileFormat,
                                 const TextureLoadInfo& TexLoadInfo,
                                 ITextureLoader**       ppLoader)
{
    try
    {
        FileWrapper File{FilePath, EFileAccessMode::Read};
        if (!File)
            LOG_ERROR_AND_THROW("Failed to open file '", FilePath, "'.");

        RefCntAutoPtr<IDataBlob> pFileData{MakeNewRCObj<DataBlobImpl>()(0)};
        File->Read(pFileData);

        RefCntAutoPtr<ITextureLoader> pTexLoader{
            MakeNewRCObj<TextureLoaderImpl>()(TexLoadInfo, reinterpret_cast<const Uint8*>(pFileData->GetConstDataPtr()), pFileData->GetSize(), std::move(pFileData)) //
        };
        if (pTexLoader)
            pTexLoader->QueryInterface(IID_TextureLoader, reinterpret_cast<IObject**>(ppLoader));
    }
    catch (std::runtime_error& err)
    {
        LOG_ERROR("Failed to create texture loader from file: ", err.what());
    }
}

void CreateTextureLoaderFromMemory(const void*            pData,
                                   size_t                 Size,
                                   IMAGE_FILE_FORMAT      FileFormat,
                                   bool                   MakeDataCopy,
                                   const TextureLoadInfo& TexLoadInfo,
                                   ITextureLoader**       ppLoader)
{
    VERIFY_EXPR(pData != nullptr && Size > 0);
    try
    {
        RefCntAutoPtr<IDataBlob> pDataCopy;
        if (MakeDataCopy)
        {
            pDataCopy = MakeNewRCObj<DataBlobImpl>()(Size, pData);
            pData     = pDataCopy->GetConstDataPtr();
        }
        RefCntAutoPtr<ITextureLoader> pTexLoader{MakeNewRCObj<TextureLoaderImpl>()(TexLoadInfo, reinterpret_cast<const Uint8*>(pData), Size, std::move(pDataCopy))};
        if (pTexLoader)
            pTexLoader->QueryInterface(IID_TextureLoader, reinterpret_cast<IObject**>(ppLoader));
    }
    catch (std::runtime_error& err)
    {
        LOG_ERROR("Failed to create texture loader from memory: ", err.what());
    }
}

void CreateTextureLoaderFromImage(Image*                 pSrcImage,
                                  const TextureLoadInfo& TexLoadInfo,
                                  ITextureLoader**       ppLoader)
{
    VERIFY_EXPR(pSrcImage != nullptr);
    try
    {
        RefCntAutoPtr<ITextureLoader> pTexLoader{MakeNewRCObj<TextureLoaderImpl>()(TexLoadInfo, pSrcImage)};
        if (pTexLoader)
            pTexLoader->QueryInterface(IID_TextureLoader, reinterpret_cast<IObject**>(ppLoader));
    }
    catch (std::runtime_error& err)
    {
        LOG_ERROR("Failed to create texture loader from memory: ", err.what());
    }
}

} // namespace Diligent

extern "C"
{
    void Diligent_CreateTextureLoaderFromFile(const char*                      FilePath,
                                              Diligent::IMAGE_FILE_FORMAT      FileFormat,
                                              const Diligent::TextureLoadInfo& TexLoadInfo,
                                              Diligent::ITextureLoader**       ppLoader)
    {
        Diligent::CreateTextureLoaderFromFile(FilePath, FileFormat, TexLoadInfo, ppLoader);
    }

    void Diligent_CreateTextureLoaderFromMemory(const void*                      pData,
                                                size_t                           Size,
                                                Diligent::IMAGE_FILE_FORMAT      FileFormat,
                                                bool                             MakeCopy,
                                                const Diligent::TextureLoadInfo& TexLoadInfo,
                                                Diligent::ITextureLoader**       ppLoader)
    {
        Diligent::CreateTextureLoaderFromMemory(pData, Size, FileFormat, MakeCopy, TexLoadInfo, ppLoader);
    }

    void Diligent_CreateTextureLoaderFromImage(Diligent::Image*                 pSrcImage,
                                               const Diligent::TextureLoadInfo& TexLoadInfo,
                                               Diligent::ITextureLoader**       ppLoader)
    {
        Diligent::CreateTextureLoaderFromImage(pSrcImage, TexLoadInfo, ppLoader);
    }
}
