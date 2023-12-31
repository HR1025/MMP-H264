//
// H264SliceDecodingProcess.h
//
// Library: Codec
// Package: H264
// Module:  H264
// 

#include "H264Common.h"

#include <functional>

namespace Mmp
{
namespace Codec
{

/**
 * @sa  8.2 Slice decoding process - ISO 14496/10(2020)
 */
class H264SliceDecodingProcess
{
public:
    using ptr = std::shared_ptr<H264SliceDecodingProcess>;
public:
    H264SliceDecodingProcess();
    ~H264SliceDecodingProcess();
public:
    void SliceDecodingProcess(H264NalSyntax::ptr nal);
public:
    H264PictureContext::ptr GetCurrentPictureContext();
    H264PictureContext::cache GetAllPictures();
    std::vector<H264PictureContext::ptr> GetRefPicList0();
    std::vector<H264PictureContext::ptr> GetRefPicList1();
private:
    using task = std::function<void()>;
private:
    void OnDecodingBegin();
    void OnDecodingEnd();
private:
    void DecodingProcessForPictureOrderCount(H264NalSyntax::ptr nal, H264SpsSyntax::ptr sps, H264PpsSyntax::ptr pps, H264SliceHeaderSyntax::ptr slice, uint8_t nal_ref_idc, H264PictureContext::ptr picture);
    void DecodeH264PictureOrderCountType0(H264PictureContext::ptr prevPictrue, H264SpsSyntax::ptr sps, H264PpsSyntax::ptr pps, H264SliceHeaderSyntax::ptr slice, uint8_t nal_ref_idc, H264PictureContext::ptr picture);
    void DecodeH264PictureOrderCountType1(H264PictureContext::ptr prevPictrue, H264NalSyntax::ptr nal, H264SpsSyntax::ptr sps, H264SliceHeaderSyntax::ptr slice, uint8_t nal_ref_idc, H264PictureContext::ptr picture);
    void DecodeH264PictureOrderCountType2(H264PictureContext::ptr prevPictrue, H264SpsSyntax::ptr sps, H264SliceHeaderSyntax::ptr slice, uint8_t nal_ref_idc, H264PictureContext::ptr picture);
private:
    void DecodingProcessForReferencePictureListsConstruction(H264SliceHeaderSyntax::ptr slice, H264SpsSyntax::ptr sps, H264PictureContext::cache pictures, H264PictureContext::ptr picture);
    void DecodingProcessForPictureNumbers(H264SliceHeaderSyntax::ptr slice, H264SpsSyntax::ptr sps, H264PictureContext::cache pictures, H264PictureContext::ptr picture);
    void InitializationProcessForReferencePictureLists(H264SliceHeaderSyntax::ptr slice, H264PictureContext::cache pictures, H264PictureContext::ptr picture);
    void ModificationProcessForReferencePictureLists(H264SliceHeaderSyntax::ptr slice, H264SpsSyntax::ptr sps, H264PictureContext::cache pictures, H264PictureContext::ptr picture);
private:
    void DecodeReferencePictureMarkingProcess(H264NalSyntax::ptr nal, H264SliceHeaderSyntax::ptr slice, H264SpsSyntax::ptr sps, H264PictureContext::cache pictures, H264PictureContext::ptr picture, uint8_t nal_ref_idc);
    void DecodingProcessForGapsInFrameNum(H264SliceHeaderSyntax::ptr slice, H264SpsSyntax::ptr sps, H264PictureContext::ptr picture, uint64_t PrevRefFrameNum);
    void SequenceOfOperationsForDecodedReferencePictureMarkingProcess(H264NalSyntax::ptr nal, H264SliceHeaderSyntax::ptr slice, H264SpsSyntax::ptr sps, H264PictureContext::cache pictures, H264PictureContext::ptr picture);
    void SlidingWindowDecodedReferencePictureMarkingProcess(H264SliceHeaderSyntax::ptr slice, H264SpsSyntax::ptr sps, H264PictureContext::cache pictures, H264PictureContext::ptr picture);
    void AdaptiveMemoryControlDecodedReferencePicutreMarkingPorcess(H264SliceHeaderSyntax::ptr slice, H264PictureContext::cache pictures, H264PictureContext::ptr picture);
private:
    H264PictureContext::ptr _prevPicture;
private:
    std::vector<H264PictureContext::ptr> _RefPicList0;
    std::vector<H264PictureContext::ptr> _RefPicList1;
private:
    uint64_t _curId;
    H264PictureContext::cache _pictures;
    std::map<uint32_t, H264SpsSyntax::ptr> _spss;
    std::map<uint32_t, H264PpsSyntax::ptr> _ppss;
private:
    std::vector<task> _beginTasks;
    std::vector<task> _endTasks;
};

} // namespace Codec
} // namespace Mmp