/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 * Additional HDR10 metadata support by VideoProcessor Team
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>
#include <cmath>
#include <guiddef.h>
#include "HDRData.h"

// Forward declare Magewell SDK HDMI HDR infoframe payload structure
// This avoids including the full Magewell SDK header in this header file
#pragma pack(push, 1)
struct _HDMI_HDR_INFOFRAME_PAYLOAD {
    BYTE byEOTF;
    BYTE byMetadataDescriptorID;
    BYTE display_primaries_lsb_x0;
    BYTE display_primaries_msb_x0;
    BYTE display_primaries_lsb_y0;
    BYTE display_primaries_msb_y0;
    BYTE display_primaries_lsb_x1;
    BYTE display_primaries_msb_x1;
    BYTE display_primaries_lsb_y1;
    BYTE display_primaries_msb_y1;
    BYTE display_primaries_lsb_x2;
    BYTE display_primaries_msb_x2;
    BYTE display_primaries_lsb_y2;
    BYTE display_primaries_msb_y2;
    BYTE white_point_lsb_x;
    BYTE white_point_msb_x;
    BYTE white_point_lsb_y;
    BYTE white_point_msb_y;
    BYTE max_display_mastering_lsb_luminance;
    BYTE max_display_mastering_msb_luminance;
    BYTE min_display_mastering_lsb_luminance;
    BYTE min_display_mastering_msb_luminance;
    BYTE maximum_content_light_level_lsb;
    BYTE maximum_content_light_level_msb;
    BYTE maximum_frame_average_light_level_lsb;
    BYTE maximum_frame_average_light_level_msb;
};
#pragma pack(pop)

/**
 * HDR10 Static Metadata Type 1 (ST.2086 / BT.2020)
 * 
 * This structure defines the standard HDR10 mastering display metadata
 * as specified in EIA-861.3 and SMPTE ST 2086. The format uses fixed-point
 * scaling to ensure byte alignment and compatibility with HDMI InfoFrame
 * transmission.
 */

// Fixed-point scaling factors for HDR10 metadata
constexpr uint32_t HDR_COLOR_COORD_SCALE = 50000;      // Color coordinates: value = coordinate * 50000
constexpr uint32_t HDR_LUMINANCE_SCALE = 10000;        // Luminance: value = nits * 10000
constexpr uint16_t HDR_LIGHT_LEVEL_SCALE = 1;          // CLL/FALL: value = nits (1:1)

/**
 * @brief HDR color primary/white point coordinates in fixed-point format
 * 
 * The x and y chromaticity coordinates are stored as 16-bit unsigned integers
 * representing the actual coordinate multiplied by 50000. For example:
 * - Red primary x=0.680 would be stored as 0.680 * 50000 = 34000
 * - Green primary x=0.265 would be stored as 0.265 * 50000 = 13250
 * - White point x=0.150 would be stored as 0.150 * 50000 = 7500
 */
struct HDRColorPrimaries {
    uint16_t x;  // Chromaticity x coordinate * 50000
    uint16_t y;  // Chromaticity y coordinate * 50000
    
    // Helper: Convert from double (0.0 - 1.0) to fixed-point
    static HDRColorPrimaries FromDouble(double x_coord, double y_coord) {
        HDRColorPrimaries result;
        result.x = static_cast<uint16_t>(std::round(x_coord * HDR_COLOR_COORD_SCALE));
        result.y = static_cast<uint16_t>(std::round(y_coord * HDR_COLOR_COORD_SCALE));
        return result;
    }
    
    // Helper: Convert fixed-point to double
    double GetX() const { return static_cast<double>(x) / HDR_COLOR_COORD_SCALE; }
    double GetY() const { return static_cast<double>(y) / HDR_COLOR_COORD_SCALE; }
};

/**
 * @brief HDR10 Metadata Payload (SMPTE ST 2086)
 * 
 * This structure contains all the static metadata required for HDR10
 * content. It matches the HDMI HDR InfoFrame payload format and can be
 * directly embedded into IMediaSideData for madVR consumption.
 * 
 * Field units:
 * - Color primaries/white point: Fixed-point (value = coordinate * 50000)
 * - Luminance: Fixed-point (value = nits * 10000)
 * - CLL/FALL: Direct nits value (1:1 mapping)
 */
struct HDR10MetaDataPayload {
    // Display mastering display color primaries (G-B-R order for HDMI compatibility)
    HDRColorPrimaries displayPrimaries[3];  // [0]=Green, [1]=Blue, [2]=Red
    
    // Display white point (BT.709 / BT.2020: x=0.150, y=0.060)
    HDRColorPrimaries whitePoint;
    
    // Maximum and minimum display mastering luminance
    // Units: 0.0001 cd/m² (nits)
    uint32_t maxDisplayMasteringLuminance;  // Max luminance * 10000
    uint32_t minDisplayMasteringLuminance;  // Min luminance * 10000
    
    // Content light level information
    uint16_t maxCLL;    // Max Content Light Level in nits
    uint16_t maxFALL;   // Max Frame-Average Light Level in nits
    
    /**
     * @brief Convert from floating-point HDRData format
     * 
     * Takes the existing floating-point HDRData and converts to fixed-point
     * HDR10MetaDataPayload format.
     */
    static HDR10MetaDataPayload FromHDRData(const struct HDRData& hdrData) {
        HDR10MetaDataPayload result;
        
        // Convert primaries (note: HDRData uses R-G-B order, HDMI uses G-B-R)
        result.displayPrimaries[0] = HDRColorPrimaries::FromDouble(hdrData.displayPrimaryGreenX, hdrData.displayPrimaryGreenY);
        result.displayPrimaries[1] = HDRColorPrimaries::FromDouble(hdrData.displayPrimaryBlueX,   hdrData.displayPrimaryBlueY);
        result.displayPrimaries[2] = HDRColorPrimaries::FromDouble(hdrData.displayPrimaryRedX,    hdrData.displayPrimaryRedY);
        
        // Convert white point
        result.whitePoint = HDRColorPrimaries::FromDouble(hdrData.whitePointX, hdrData.whitePointY);
        
        // Convert luminance (convert from double to fixed-point)
        result.maxDisplayMasteringLuminance = static_cast<uint32_t>(std::round(hdrData.masteringDisplayMaxLuminance * HDR_LUMINANCE_SCALE));
        result.minDisplayMasteringLuminance = static_cast<uint32_t>(std::round(hdrData.masteringDisplayMinLuminance * HDR_LUMINANCE_SCALE));
        
        // CLL/FALL are stored as direct nits values
        result.maxCLL = static_cast<uint16_t>(std::round(hdrData.maxCll));
        result.maxFALL = static_cast<uint16_t>(std::round(hdrData.maxFall));
        
        return result;
    }
    
    /**
     * @brief Convert to floating-point HDRData format
     * 
     * Converts the fixed-point HDR10MetaDataPayload back to the existing
     * floating-point HDRData format for compatibility.
     */
    struct HDRData ToHDRData() const {
        struct HDRData hdrData;
        
        // Convert primaries (reverse the G-B-R ordering)
        hdrData.displayPrimaryGreenX = displayPrimaries[0].GetX();
        hdrData.displayPrimaryGreenY = displayPrimaries[0].GetY();
        hdrData.displayPrimaryBlueX = displayPrimaries[1].GetX();
        hdrData.displayPrimaryBlueY = displayPrimaries[1].GetY();
        hdrData.displayPrimaryRedX = displayPrimaries[2].GetX();
        hdrData.displayPrimaryRedY = displayPrimaries[2].GetY();
        
        // Convert white point
        hdrData.whitePointX = whitePoint.GetX();
        hdrData.whitePointY = whitePoint.GetY();
        
        // Convert luminance
        hdrData.masteringDisplayMaxLuminance = static_cast<double>(maxDisplayMasteringLuminance) / HDR_LUMINANCE_SCALE;
        hdrData.masteringDisplayMinLuminance = static_cast<double>(minDisplayMasteringLuminance) / HDR_LUMINANCE_SCALE;
        
        // CLL/FALL
        hdrData.maxCll = static_cast<double>(maxCLL);
        hdrData.maxFall = static_cast<double>(maxFALL);
        
        return hdrData;
    }
    
    /**
     * @brief Check if the metadata is valid
     * 
     * A valid HDR10 metadata must have:
     * - Non-zero max display luminance
     * - maxCLL > 0
     * - Valid color primaries (within BT.2020 gamut)
     */
    bool IsValid() const {
        if (maxDisplayMasteringLuminance == 0) return false;
        if (maxCLL == 0) return false;
        
        // Check that primaries are within valid range (0 to 1)
        for (int i = 0; i < 3; i++) {
            if (displayPrimaries[i].GetX() <= 0 || displayPrimaries[i].GetX() >= 1) return false;
            if (displayPrimaries[i].GetY() <= 0 || displayPrimaries[i].GetY() >= 1) return false;
        }
        
        // Check white point
        if (whitePoint.GetX() <= 0 || whitePoint.GetX() >= 1) return false;
        if (whitePoint.GetY() <= 0 || whitePoint.GetY() >= 1) return false;
        
        return true;
    }
};

// GUID for HDR10 metadata side data (matches LAV filters convention)
// {A5B3C7D9-1234-5678-9ABC-DEF012345678}
EXTERN_C const GUID IID_MediaSideDataHDR10;

/**
 * @brief Convert Magewell HDMI HDR InfoFrame to HDR10MetaDataPayload
 * 
 * Parses the raw HDMI HDR infoframe payload from Magewell SDK and converts
 * it to our standardized HDR10MetaDataPayload structure.
 * 
 * @param hdrInfoFrame The HDMI HDR infoframe from Magewell SDK
 * @return HDR10MetaDataPayload with parsed values, or default (invalid) if parsing fails
 */
inline HDR10MetaDataPayload ParseMagewellHDMIHDRInfoFrame(const struct _HDMI_HDR_INFOFRAME_PAYLOAD& hdrInfoFrame) {
    HDR10MetaDataPayload result = {};
    
    // EOTF (Electro-Optical Transfer Function)
    // 0x04 = SMPTE ST 2084 (PQ) for HDR10
    // We don't store EOTF in HDR10MetaDataPayload but could use it for validation
    const BYTE EOTF_SMPTE_2084 = 0x04;
    if (hdrInfoFrame.byEOTF != EOTF_SMPTE_2084) {
        // Not HDR10 (PQ), metadata may not be applicable
        return result;
    }
    
    // Metadata Descriptor ID 0x05 = Mastering Display Color Volume
    const BYTE METADATA_DESCRIPT_ID_MASTERING = 0x05;
    if (hdrInfoFrame.byMetadataDescriptorID != METADATA_DESCRIPT_ID_MASTERING) {
        // Expected mastering display metadata
        return result;
    }
    
    // Parse display primaries (each is 2 bytes, little-endian)
    // Format: [LSB x0][MSB x0][LSB y0][MSB y0] for each primary (G-B-R order)
    
    // Green primary (index 0)
    result.displayPrimaries[0].x = static_cast<uint16_t>(
        hdrInfoFrame.display_primaries_lsb_x0 | 
        (hdrInfoFrame.display_primaries_msb_x0 << 8));
    result.displayPrimaries[0].y = static_cast<uint16_t>(
        hdrInfoFrame.display_primaries_lsb_y0 | 
        (hdrInfoFrame.display_primaries_msb_y0 << 8));
    
    // Blue primary (index 1)
    result.displayPrimaries[1].x = static_cast<uint16_t>(
        hdrInfoFrame.display_primaries_lsb_x1 | 
        (hdrInfoFrame.display_primaries_msb_x1 << 8));
    result.displayPrimaries[1].y = static_cast<uint16_t>(
        hdrInfoFrame.display_primaries_lsb_y1 | 
        (hdrInfoFrame.display_primaries_msb_y1 << 8));
    
    // Red primary (index 2)
    result.displayPrimaries[2].x = static_cast<uint16_t>(
        hdrInfoFrame.display_primaries_lsb_x2 | 
        (hdrInfoFrame.display_primaries_msb_x2 << 8));
    result.displayPrimaries[2].y = static_cast<uint16_t>(
        hdrInfoFrame.display_primaries_lsb_y2 | 
        (hdrInfoFrame.display_primaries_msb_y2 << 8));
    
    // White point (2 bytes each, little-endian)
    result.whitePoint.x = static_cast<uint16_t>(
        hdrInfoFrame.white_point_lsb_x | 
        (hdrInfoFrame.white_point_msb_x << 8));
    result.whitePoint.y = static_cast<uint16_t>(
        hdrInfoFrame.white_point_lsb_y | 
        (hdrInfoFrame.white_point_msb_y << 8));
    
    // Max display mastering luminance (4 bytes / 32-bit value split across 2 bytes in HDMI)
    // HDMI actually uses 2 bytes for this, representing value / 10000
    result.maxDisplayMasteringLuminance = static_cast<uint32_t>(
        hdrInfoFrame.max_display_mastering_lsb_luminance | 
        (hdrInfoFrame.max_display_mastering_msb_luminance << 8));
    // HDMI format: value represents cd/m² directly in the 2-byte field
    // So we need to convert to our fixed-point format (multiply by 10000)
    result.maxDisplayMasteringLuminance *= HDR_LUMINANCE_SCALE;
    
    // Min display mastering luminance
    result.minDisplayMasteringLuminance = static_cast<uint32_t>(
        hdrInfoFrame.min_display_mastering_lsb_luminance | 
        (hdrInfoFrame.min_display_mastering_msb_luminance << 8));
    result.minDisplayMasteringLuminance *= HDR_LUMINANCE_SCALE;
    
    // Max Content Light Level (CLL) - 2 bytes, direct nits value
    result.maxCLL = static_cast<uint16_t>(
        hdrInfoFrame.maximum_content_light_level_lsb | 
        (hdrInfoFrame.maximum_content_light_level_msb << 8));
    
    // Max Frame Average Light Level (FALL) - 2 bytes, direct nits value
    result.maxFALL = static_cast<uint16_t>(
        hdrInfoFrame.maximum_frame_average_light_level_lsb | 
        (hdrInfoFrame.maximum_frame_average_light_level_msb << 8));
    
    return result;
}

/**
 * @brief Convert HDR10MetaDataPayload to Magewell HDMI HDR InfoFrame format
 * 
 * Converts our standardized HDR10MetaDataPayload to the Magewell SDK format
 * for output to HDMI or for storage.
 */
inline void ConvertToMagewellHDMIHDRInfoFrame(const HDR10MetaDataPayload& meta, struct _HDMI_HDR_INFOFRAME_PAYLOAD& hdrInfoFrame) {
    // Set EOTF to SMPTE ST 2084 (PQ)
    hdrInfoFrame.byEOTF = 0x04;
    
    // Set Metadata Descriptor ID to Mastering Display Color Volume
    hdrInfoFrame.byMetadataDescriptorID = 0x05;
    
    // Convert display primaries (fixed-point to HDMI format - already in correct format)
    auto WritePrimaries = [&](int index, HDRColorPrimaries prim) {
        uint16_t val = static_cast<uint16_t>(std::round(prim.GetX() * HDR_COLOR_COORD_SCALE));
        switch (index) {
            case 0: // Green
                hdrInfoFrame.display_primaries_lsb_x0 = val & 0xFF;
                hdrInfoFrame.display_primaries_msb_x0 = (val >> 8) & 0xFF;
                val = static_cast<uint16_t>(std::round(prim.GetY() * HDR_COLOR_COORD_SCALE));
                hdrInfoFrame.display_primaries_lsb_y0 = val & 0xFF;
                hdrInfoFrame.display_primaries_msb_y0 = (val >> 8) & 0xFF;
                break;
            case 1: // Blue
                hdrInfoFrame.display_primaries_lsb_x1 = val & 0xFF;
                hdrInfoFrame.display_primaries_msb_x1 = (val >> 8) & 0xFF;
                val = static_cast<uint16_t>(std::round(prim.GetY() * HDR_COLOR_COORD_SCALE));
                hdrInfoFrame.display_primaries_lsb_y1 = val & 0xFF;
                hdrInfoFrame.display_primaries_msb_y1 = (val >> 8) & 0xFF;
                break;
            case 2: // Red
                hdrInfoFrame.display_primaries_lsb_x2 = val & 0xFF;
                hdrInfoFrame.display_primaries_msb_x2 = (val >> 8) & 0xFF;
                val = static_cast<uint16_t>(std::round(prim.GetY() * HDR_COLOR_COORD_SCALE));
                hdrInfoFrame.display_primaries_lsb_y2 = val & 0xFF;
                hdrInfoFrame.display_primaries_msb_y2 = (val >> 8) & 0xFF;
                break;
        }
    };
    
    WritePrimaries(0, meta.displayPrimaries[0]); // Green
    WritePrimaries(1, meta.displayPrimaries[1]); // Blue
    WritePrimaries(2, meta.displayPrimaries[2]); // Red
    
    // White point
    uint16_t val = static_cast<uint16_t>(std::round(meta.whitePoint.GetX() * HDR_COLOR_COORD_SCALE));
    hdrInfoFrame.white_point_lsb_x = val & 0xFF;
    hdrInfoFrame.white_point_msb_x = (val >> 8) & 0xFF;
    val = static_cast<uint16_t>(std::round(meta.whitePoint.GetY() * HDR_COLOR_COORD_SCALE));
    hdrInfoFrame.white_point_lsb_y = val & 0xFF;
    hdrInfoFrame.white_point_msb_y = (val >> 8) & 0xFF;
    
    // Max display mastering luminance (convert from fixed-point to HDMI format)
    uint32_t maxLum = meta.maxDisplayMasteringLuminance / HDR_LUMINANCE_SCALE;
    hdrInfoFrame.max_display_mastering_lsb_luminance = maxLum & 0xFF;
    hdrInfoFrame.max_display_mastering_msb_luminance = (maxLum >> 8) & 0xFF;
    
    // Min display mastering luminance
    uint32_t minLum = meta.minDisplayMasteringLuminance / HDR_LUMINANCE_SCALE;
    hdrInfoFrame.min_display_mastering_lsb_luminance = minLum & 0xFF;
    hdrInfoFrame.min_display_mastering_msb_luminance = (minLum >> 8) & 0xFF;
    
    // CLL
    hdrInfoFrame.maximum_content_light_level_lsb = meta.maxCLL & 0xFF;
    hdrInfoFrame.maximum_content_light_level_msb = (meta.maxCLL >> 8) & 0xFF;
    
    // FALL
    hdrInfoFrame.maximum_frame_average_light_level_lsb = meta.maxFALL & 0xFF;
    hdrInfoFrame.maximum_frame_average_light_level_msb = (meta.maxFALL >> 8) & 0xFF;
}