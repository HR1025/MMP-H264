//
// H264Deserialize.h
//
// Library: Coedec
// Package: H264
// Module:  H264
// 

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "H264Common.h"
#include "H264BinaryReader.h"

namespace Mmp
{
namespace Codec
{

/**
 * @sa ISO 14496/10(2020) - 8.1 NAL unit decoding process
 */
class H264Deserialize
{
public:
    using ptr = std::shared_ptr<H264Deserialize>;
public:
    H264Deserialize();
    ~H264Deserialize();
public:
    /**
     * @note for H264 Annex B type, common in network stream
     */
    bool DeserializeByteStreamNalUnit(H264BinaryReader::ptr br, H264NalSyntax::ptr nal);
    bool DeserializeNalSyntax(H264BinaryReader::ptr br, H264NalSyntax::ptr nal);
    bool DeserializeHrdSyntax(H264BinaryReader::ptr br, H264HrdSyntax::ptr hrd);
    bool DeserializeVuiSyntax(H264BinaryReader::ptr br, H264VuiSyntax::ptr vui);
    bool DeserializeSeiSyntax(H264BinaryReader::ptr br, H264SeiSyntax::ptr sei);
    bool DeserializeSpsSyntax(H264BinaryReader::ptr br, H264SpsSyntax::ptr sps);
    bool DeserializeSliceHeaderSyntax(H264BinaryReader::ptr br, H264NalSyntax::ptr nal, H264SliceHeaderSyntax::ptr slice);
    bool DeserializeDecodedReferencePictureMarkingSyntax(H264BinaryReader::ptr br, H264NalSyntax::ptr nal, H264DecodedReferencePictureMarkingSyntax::ptr drpm);
    bool DeserializeSubSpsSyntax(H264BinaryReader::ptr br, H264SpsSyntax::ptr sps, H264SubSpsSyntax::ptr subSps);
    bool DeserializeSpsMvcSyntax(H264BinaryReader::ptr br, H264SpsMvcSyntax::ptr mvc);
    bool DeserializeMvcVuiSyntax(H264BinaryReader::ptr br, H264MvcVuiSyntax::ptr mvcVui);
    bool DeserializePpsSyntax(H264BinaryReader::ptr br, H264PpsSyntax::ptr pps);
private:
    bool DeserializeNalSvcSyntax(H264BinaryReader::ptr br, H264NalSvcSyntax::ptr svc);
    bool DeserializeNal3dAvcSyntax(H264BinaryReader::ptr br, H264Nal3dAvcSyntax::ptr avc);
    bool DeserializeNalMvcSyntax(H264BinaryReader::ptr br, H264NalMvcSyntax::ptr mvc);
    bool DeserializeScalingListSyntax(H264BinaryReader::ptr br, std::vector<int32_t>& scalingList, int32_t sizeOfScalingList, int32_t& useDefaultScalingMatrixFlag);
    bool DeserializeReferencePictureListModificationSyntax(H264BinaryReader::ptr br, H264SliceHeaderSyntax::ptr slice, H264ReferencePictureListModificationSyntax::ptr rplm);
    bool DeserializePredictionWeightTableSyntax(H264BinaryReader::ptr br, H264SpsSyntax::ptr sps, H264SliceHeaderSyntax::ptr slice, H264PredictionWeightTableSyntax::ptr pwt);
private: /* SEI */
    bool DeserializeSeiBufferPeriodSyntax(H264BinaryReader::ptr br, H264SeiBufferPeriodSyntax::ptr bp);
    bool DeserializeSeiPictureTimingSyntax(H264BinaryReader::ptr br, H264VuiSyntax::ptr vui, H264SeiPictureTimingSyntax::ptr pt);
    bool DeserializeSeiUserDataRegisteredSyntax(H264BinaryReader::ptr br, uint32_t payloadSize, H264SeiUserDataRegisteredSyntax::ptr udr);
    bool DeserializeSeiUserDataUnregisteredSyntax(H264BinaryReader::ptr br, uint32_t payloadSize, H264SeiUserDataUnregisteredSyntax::ptr udn);
    bool DeserializeSeiRecoveryPointSyntax(H264BinaryReader::ptr br, H264SeiRecoveryPointSyntax::ptr pt);
    bool DeserializeSeiContentLigntLevelInfoSyntax(H264BinaryReader::ptr br, H264SeiContentLigntLevelInfoSyntax::ptr clli);
    bool DeserializeSeiDisplayOrientationSyntax(H264BinaryReader::ptr br, H264SeiDisplayOrientationSyntax::ptr dot);
    bool DeserializeSeiMasteringDisplayColourVolumeSyntax(H264BinaryReader::ptr br, H264MasteringDisplayColourVolumeSyntax::ptr mdcv);
    bool DeserializeSeiFilmGrainSyntax(H264BinaryReader::ptr br, H264SeiFilmGrainSyntax::ptr fg);
    bool DeserializeSeiFramePackingArrangementSyntax(H264BinaryReader::ptr br, H264SeiFramePackingArrangementSyntax::ptr fpa);
    bool DeserializeSeiAlternativeTransferCharacteristicsSyntax(H264BinaryReader::ptr br, H264SeiAlternativeTransferCharacteristicsSyntax::ptr atc);
    bool DeserializeAmbientViewingEnvironmentSyntax(H264BinaryReader::ptr br, H264AmbientViewingEnvironmentSyntax::ptr awe);
private:
    H264ContextSyntax::ptr _contex;
};

} // namespace Codec
} // namespace Mmp