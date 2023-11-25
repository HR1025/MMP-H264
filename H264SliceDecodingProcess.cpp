#include "H264SliceDecodingProcess.h"

#include <cassert>
#include <algorithm>

namespace Mmp
{
namespace Codec
{

constexpr int64_t no_long_term_frame_indices = -1;

static bool PictureIsSecondField(H264PictureContext::ptr picture)
{
    return picture->bottom_field_flag == 1;
}

static H264PictureContext::ptr /* complementary picture */ FindComplementaryPicture(H264PictureContext::cache pictures, H264PictureContext::ptr picture)
{
    H264PictureContext::ptr compPicture = nullptr;
    assert(false); // TODO : long term picture
    if (PictureIsSecondField(picture)) // second filed
    {
        for (auto& _picture : pictures)
        {
            if (_picture->PicNum /* second field */ == picture->PicNum - 1 /* first filed */)
            {
                compPicture = _picture;
                break;
            }
        }
    }
    else if (picture->bottom_field_flag == 0 /* && picture->bottom_field_flag == 1 */) // first filed
    {
        for (auto& _picture : pictures)
        {
            if (_picture->PicNum /* first filed */ == picture->PicNum + 1 /* second field */)
            {
                compPicture = _picture;
                break;
            }
        }
    }
    return compPicture;
}


static uint64_t GetPicNumX(H264SliceHeaderSyntax::ptr slice)
{
    uint64_t picNumX = 0; // (8-39)
    {
        // The variable CurrPicNum is derived as follows:
        // - If field_pic_flag is equal to 0, CurrPicNum is set equal to frame_num.
        // - Otherwise (field_pic_flag is equal to 1), CurrPicNum is set equal to 2 * frame_num + 1
        uint64_t CurrPicNum = 0;
        if (slice->field_pic_flag == 0)
        {
            CurrPicNum = slice->frame_num;
        }
        else if (slice->field_pic_flag == 1)
        {
            CurrPicNum = 2 * slice->frame_num + 1;
        }
        picNumX = CurrPicNum - (slice->drpm->difference_of_pic_nums_minus1 + 1);
    }
    return picNumX;
}

static void UnMarkUsedForShortTermReference(H264PictureContext::cache pictures, uint64_t picNumX)
{
    for (auto _picture : pictures)
    {
        if (_picture->PicNum == picNumX)
        {
            if (_picture->field_pic_flag == 0)
            {
                _picture->referenceFlag = H264PictureContext::unused_for_reference;
                H264PictureContext::ptr compPicture = FindComplementaryPicture(pictures, _picture);
                if (compPicture)
                {
                    compPicture->referenceFlag = H264PictureContext::unused_for_reference;
                }
            }
            else if (_picture->field_pic_flag == 1)
            {
                _picture->referenceFlag = _picture->referenceFlag ^ H264PictureContext::used_for_short_term_reference;
                H264PictureContext::ptr compPicture = FindComplementaryPicture(pictures, _picture);
                if (compPicture)
                {
                    compPicture->referenceFlag = compPicture->referenceFlag ^ H264PictureContext::used_for_short_term_reference;
                }
            }
        }
    }
}

static void UnMarkUsedForLongTermReference(H264PictureContext::cache pictures, uint32_t long_term_pic_num)
{
    for (auto _picture : pictures)
    {
        if (long_term_pic_num == _picture->LongTermPicNum)
        {
            if (_picture->field_pic_flag == 0)
            {
                _picture->referenceFlag = H264PictureContext::unused_for_reference;
                H264PictureContext::ptr compPicture = FindComplementaryPicture(pictures, _picture);
                if (compPicture)
                {
                    compPicture->referenceFlag = H264PictureContext::unused_for_reference;
                }
            }
            else if (_picture->field_pic_flag == 1)
            {
                _picture->referenceFlag = _picture->referenceFlag ^ H264PictureContext::used_for_long_term_reference;
                H264PictureContext::ptr compPicture = FindComplementaryPicture(pictures, _picture);
                if (compPicture)
                {
                    compPicture->referenceFlag = compPicture->referenceFlag ^ H264PictureContext::used_for_long_term_reference;
                }
            }
        }
    }
}

static void UnMarkUsedForReference(H264PictureContext::cache pictures, uint32_t long_term_pic_num)
{
    for (auto _picture : pictures)
    {
        if (long_term_pic_num == _picture->LongTermPicNum)
        {
            _picture->referenceFlag = H264PictureContext::unused_for_reference;
            H264PictureContext::ptr compPicture = FindComplementaryPicture(pictures, _picture);
            if (compPicture)
            {
                compPicture->referenceFlag = H264PictureContext::unused_for_reference;
            }
        }
    }
}

static void MarkShortTermReferenceToLongTermReference(H264PictureContext::cache pictures, uint64_t picNumX, uint32_t long_term_frame_idx)
{
    for (auto _picture : pictures)
    {
        if (_picture->field_pic_flag == 0)
        {
            _picture->referenceFlag = H264PictureContext::used_for_long_term_reference;
            _picture->LongTermFrameIdx = long_term_frame_idx;
            H264PictureContext::ptr compPicture = FindComplementaryPicture(pictures, _picture);
            if (compPicture)
            {
                compPicture->referenceFlag = H264PictureContext::used_for_long_term_reference;
                compPicture->LongTermFrameIdx = long_term_frame_idx;
            }
        }
    }
}

/*************************************** 8.2.1 Decoding process for picture order count(Begin) ******************************************/

static int32_t PicOrderCnt(H264PictureContext::ptr picX) // (8-1)
{
    if (picX->field_pic_flag == 0)
    {
        return std::min(picX->TopFieldOrderCnt, picX->BottomFieldOrderCnt);
    }
    else if (picX->bottom_field_flag == 0 /* && picX->field_pic_flag == 1 */)
    {
        return picX->TopFieldOrderCnt;
    }
    else if (picX->bottom_field_flag == 1 /* && picX->field_pic_flag == 1 */)
    {
        return picX->BottomFieldOrderCnt;
    }
    else
    {
        assert(false);
        return 0;
    }
}

static int32_t DiffPicOrderCnt(H264PictureContext::ptr picA, H264PictureContext::ptr picB) // (8-2)
{
    return PicOrderCnt(picA) - PicOrderCnt(picB);
}

/**
 * @sa  ISO 14496/10(2020) - 8.2.1 Decoding process for picture order count
 */
void H264SliceDecodingProcess::DecodingProcessForPictureOrderCount(H264SpsSyntax::ptr sps, H264SliceHeaderSyntax::ptr slice, uint8_t nal_ref_idc, H264PictureContext::ptr picture)
{
    if (sps->pic_order_cnt_type > 2)
    {
        assert(false);
        return;
    }

    if (sps->pic_order_cnt_type == 0)
    {
        DecodeH264PictureOrderCountType0(_prevPicture, sps, slice, picture);
    }
    else if (sps->pic_order_cnt_type == 1)
    {
        DecodeH264PictureOrderCountType1(_prevPicture, sps, slice, nal_ref_idc, picture);
    }
    else if (sps->pic_order_cnt_type == 2)
    {
        DecodeH264PictureOrderCountType2(_prevPicture, sps,  slice, nal_ref_idc, picture);
    }
    
    // Hint :
    //        When the current picture includes a
    //        memory_management_control_operation equal to 5, after the decoding of the current picture, tempPicOrderCnt is set
    //        equal to PicOrderCnt( CurrPic ), TopFieldOrderCnt of the current picture (if any) is set equal to
    //        TopFieldOrderCnt − tempPicOrderCnt, and BottomFieldOrderCnt of the current picture (if any) is set equal to
    //        BottomFieldOrderCnt − tempPicOrderCnt.
    _endTasks.push_back([picture]()
    {
        if (picture->memory_management_control_operations.count(5/* H264MmcoType::MMP_H264_MMOO_5 */))
        {
            int32_t tempPicOrderCnt = PicOrderCnt(picture);
            picture->TopFieldOrderCnt = picture->TopFieldOrderCnt - tempPicOrderCnt;
            picture->BottomFieldOrderCnt = picture->BottomFieldOrderCnt - tempPicOrderCnt;
        }
    });
}

/**
 * @sa ISO 14496/10(2020) - 8.2.1.1 Decoding process for picture order count type 0 
 */
void H264SliceDecodingProcess::DecodeH264PictureOrderCountType0(H264PictureContext::ptr prevPictrue, H264SpsSyntax::ptr sps, H264SliceHeaderSyntax::ptr slice, H264PictureContext::ptr picture)
{
    int32_t prevPicOrderCntMsb = 0;
    int32_t prevPicOrderCntLsb = 0;
    int32_t PicOrderCntMsb = 0;
    // determine prevPicOrderCntMsb and prevPicOrderCntLsb
    {
        if (slice->slice_type == H264SliceType::MMP_H264_I_SLICE)
        {
            prevPicOrderCntMsb = 0;
            prevPicOrderCntLsb = 0;
        }
        else
        {
            if (prevPictrue->memory_management_control_operations.count(5/* H264MmcoType::MMP_H264_MMOO_5 */))
            {
                if (!prevPictrue->bottom_field_flag)
                {
                    prevPicOrderCntMsb = 0;
                    prevPicOrderCntLsb  = prevPictrue->TopFieldOrderCnt;
                }
                else
                {
                    prevPicOrderCntMsb = 0;
                    prevPicOrderCntLsb = 0;
                }
            }
            else
            {
                prevPicOrderCntMsb = prevPictrue->prevPicOrderCntMsb;
                prevPicOrderCntLsb = prevPictrue->pic_order_cnt_lsb;
            }
        }
    }
    // determine PicOrderCntMsb (8-3)
    {
        int64_t MaxPicOrderCntLsb = 1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4); // (7-11)
        if ((slice->pic_order_cnt_lsb < prevPicOrderCntLsb) &&
            ((prevPicOrderCntLsb - slice->pic_order_cnt_lsb) >= (MaxPicOrderCntLsb / 2))
        )
        {
            PicOrderCntMsb = prevPicOrderCntMsb + MaxPicOrderCntLsb;
        }
        else if ((slice->pic_order_cnt_lsb > prevPicOrderCntLsb) && 
            ((slice->pic_order_cnt_lsb - prevPicOrderCntLsb) > (MaxPicOrderCntLsb / 2))
        )
        {
            PicOrderCntMsb = prevPicOrderCntMsb - MaxPicOrderCntLsb;
        }
        else
        {
            PicOrderCntMsb = prevPicOrderCntMsb;
        }
    }
    // determine TopFieldOrderCnt and BottomFieldOrderCnt (8-4) and (8-5)
    {
        picture->TopFieldOrderCnt = PicOrderCntMsb + slice->pic_order_cnt_lsb;
        if (!slice->field_pic_flag)
        {
            picture->BottomFieldOrderCnt = picture->TopFieldOrderCnt + slice->delta_pic_order_cnt_bottom;
        }
        else
        {
            picture->BottomFieldOrderCnt = PicOrderCntMsb + slice->pic_order_cnt_lsb;
        }
    }
    // update context
    picture->prevPicOrderCntMsb = PicOrderCntMsb;
}

/**
 * @sa ISO 14496/10(2020) - 8.2.1.2 Decoding process for picture order count type 1 
 */
void H264SliceDecodingProcess::DecodeH264PictureOrderCountType1(H264PictureContext::ptr prevPictrue, H264SpsSyntax::ptr sps, H264SliceHeaderSyntax::ptr slice, uint8_t nal_ref_idc, H264PictureContext::ptr picture)
{
    int32_t  prevFrameNumOffset = 0;
    int64_t  absFrameNum = 0;
    int64_t  picOrderCntCycleCnt = 0;
    int64_t  frameNumInPicOrderCntCycle = 0;
    int64_t  expectedPicOrderCnt;
    // determine prevFrameNumOffset
    {
        if (slice->slice_type != H264SliceType::MMP_H264_I_SLICE)
        {
            if (prevPictrue->memory_management_control_operations.count(5 /* H264MmcoType::MMP_H264_MMOO_5 */))
            {
                prevFrameNumOffset = 0;
            }
            else
            {
                prevFrameNumOffset = prevPictrue->FrameNumOffset;
            }
        }
    }
    // determine FrameNumOffset (8-6)
    {
        if (slice->slice_type == H264SliceType::MMP_H264_I_SLICE)
        {
            picture->FrameNumOffset = 0;
        }
        else if (prevPictrue->FrameNum > slice->frame_num)
        {
            uint64_t MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4); // (7-10)
            picture->FrameNumOffset = prevFrameNumOffset + MaxFrameNum;
        }
        else
        {
            picture->FrameNumOffset = prevFrameNumOffset;
        }
    }
    // determine absFrameNum (8-7)
    {
        if (sps->num_ref_frames_in_pic_order_cnt_cycle != 0)
        {
            absFrameNum = picture->FrameNumOffset + slice->frame_num;
        }
        else
        {
            absFrameNum = 0;
        }
        if (nal_ref_idc == 0 && absFrameNum > 0)
        {
            absFrameNum = absFrameNum - 1;
        }
    }
    // determine picOrderCntCycleCnt and frameNumInPicOrderCntCycle (8-8)
    {
        if (absFrameNum > 0)
        {
            picOrderCntCycleCnt = (absFrameNum - 1) / sps->num_ref_frames_in_pic_order_cnt_cycle;
            frameNumInPicOrderCntCycle = (absFrameNum - 1) % sps->num_ref_frames_in_pic_order_cnt_cycle;
        }
    }
    // determine expectedPicOrderCnt (8-9)
    {
        if (absFrameNum > 0)
        {
            int64_t ExpectedDeltaPerPicOrderCntCycle = 0; // (7-12)
            {
                for (uint32_t i=0; i<sps->num_ref_frames_in_pic_order_cnt_cycle; i++)
                {
                    ExpectedDeltaPerPicOrderCntCycle += sps->offset_for_ref_frame[i];
                }
            }
            expectedPicOrderCnt = picOrderCntCycleCnt * ExpectedDeltaPerPicOrderCntCycle;
            for (int64_t i=0; i<=frameNumInPicOrderCntCycle; i++)
            {
                expectedPicOrderCnt = expectedPicOrderCnt + sps->offset_for_ref_frame[i];
            }
        }
        else
        {
            expectedPicOrderCnt = 0;
        }
        if (nal_ref_idc == 0)
        {
            expectedPicOrderCnt = expectedPicOrderCnt + sps->offset_for_non_ref_pic;
        }
    }
    // determine TopFieldOrderCnt and BottomFieldOrderCnt (8-10)
    {
        if (!slice->field_pic_flag)
        {
            picture->TopFieldOrderCnt = expectedPicOrderCnt + slice->delta_pic_order_cnt[0];
            picture->BottomFieldOrderCnt = picture->TopFieldOrderCnt + sps->offset_for_top_to_bottom_field + slice->delta_pic_order_cnt[1];
        }
        else if (!slice->bottom_field_flag)
        {
            picture->TopFieldOrderCnt = expectedPicOrderCnt + slice->delta_pic_order_cnt[0];
        }
        else
        {
            picture->BottomFieldOrderCnt = expectedPicOrderCnt + sps->offset_for_top_to_bottom_field + slice->delta_pic_order_cnt[0];
        }
    }
}

/**
 * @sa ISO 14496/10(2020) - 8.2.1.3 Decoding process for picture order count type 2
 */
void H264SliceDecodingProcess::DecodeH264PictureOrderCountType2(H264PictureContext::ptr prevPictrue, H264SpsSyntax::ptr sps, H264SliceHeaderSyntax::ptr slice, uint8_t nal_ref_idc, H264PictureContext::ptr picture)
{
    int64_t FrameNumOffset = 0;
    int64_t tempPicOrderCnt = 0;
    int64_t prevFrameNumOffset = 0;
    // determine prevFrameNumOffset
    {
        if (slice->slice_type != H264SliceType::MMP_H264_I_SLICE)
        {
            if (prevPictrue->memory_management_control_operations.count(5 /* H264MmcoType::MMP_H264_MMOO_5 */))
            {
                prevFrameNumOffset = 0;
            }
            else
            {
                prevFrameNumOffset = prevPictrue->FrameNumOffset;
            }
        }
    }
    // determine FrameNumOffset (8-11)
    {
        if (slice->slice_type == H264SliceType::MMP_H264_I_SLICE)
        {
            FrameNumOffset = 0;
        }
        else if (prevFrameNumOffset > slice->frame_num)
        {
            uint64_t MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4); // (7-10)
            FrameNumOffset = prevFrameNumOffset + MaxFrameNum;
        }
        else
        {
            FrameNumOffset = prevFrameNumOffset;
        }
        picture->FrameNumOffset = FrameNumOffset;
    }
    // determine tempPicOrderCnt (8-12)
    {
        if (slice->slice_type == H264SliceType::MMP_H264_I_SLICE)
        {
            tempPicOrderCnt = 0;
        }
        else if (nal_ref_idc == 0)
        {
            tempPicOrderCnt = 2 * (FrameNumOffset + slice->frame_num) - 1;
        }
        else
        {
            tempPicOrderCnt = 2 * (FrameNumOffset + slice->frame_num);
        }
    }
    // determine TopFieldOrderCnt and BottomFieldOrderCnt (8-13)
    {
        if (!slice->field_pic_flag)
        {
            picture->TopFieldOrderCnt = tempPicOrderCnt;
            picture->BottomFieldOrderCnt = tempPicOrderCnt;
        }
        else if (slice->bottom_field_flag)
        {
            picture->BottomFieldOrderCnt = tempPicOrderCnt;
        }
        else
        {
            picture->TopFieldOrderCnt = tempPicOrderCnt;
        }
    }
}

/*************************************** 8.2.1 Decoding process for picture order count(End) ******************************************/

/*************************************** 8.2.4 Decoding process for reference picture lists construction(Begin) ******************************************/

/**
 * @sa  ISO 14496/10(2020) - 8.2.4 Decoding process for reference picture lists construction
 */
void H264SliceDecodingProcess::DecodingProcessForReferencePictureListsConstruction(H264SliceHeaderSyntax::ptr slice, H264SpsSyntax::ptr sps, H264PictureContext::cache pictures, H264PictureContext::ptr picture)
{
    DecodingProcessForPictureNumbers(slice, sps, pictures, picture);
    InitializationProcessForReferencePictureLists(slice, pictures);
    if (slice->rplm->ref_pic_list_modification_flag_l0 || 
        (slice->rplm->ref_pic_list_modification_flag_l1 && slice->slice_type == H264SliceType::MMP_H264_B_SLICE)
    )
    {
        ModificationProcessForReferencePictureLists(slice, sps, pictures, picture);
    }
}

/**
 * @sa  ISO 14496/10(2020) - 8.2.4.1 Decoding process for picture numbers
 */
void H264SliceDecodingProcess::DecodingProcessForPictureNumbers(H264SliceHeaderSyntax::ptr slice, H264SpsSyntax::ptr sps, H264PictureContext::cache pictures, H264PictureContext::ptr picture)
{
    // determine FrameNumWrap (8-27)
    {
        uint64_t MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4); // (7-10)
        for (auto _picture : pictures)
        {
            if (_picture->referenceFlag & H264PictureContext::used_for_short_term_reference)
            {
                if (_picture->FrameNum > slice->frame_num)
                {
                    _picture->FrameNumWrap = _picture->FrameNum - MaxFrameNum;
                }
                else
                {
                    _picture->FrameNumWrap = _picture->FrameNum;
                }
            }
        }
    }
    // determine PicNum and LongTermPicNum
    {
        for (auto _picture : pictures)
        {
            if (_picture->field_pic_flag == 0)
            {
                if (_picture->referenceFlag & H264PictureContext::used_for_short_term_reference)
                {
                    _picture->PicNum = _picture->FrameNumWrap; // (8-28)
                }
                if (_picture->referenceFlag & H264PictureContext::used_for_long_term_reference)
                {
                    _picture->LongTermPicNum = _picture->LongTermFrameIdx; // (8-29)
                }
            }
            else if (_picture->field_pic_flag == 1)
            {
                // TOCHECK : top field 一定出现在 bottom field 之前吗, 如何确定 same parity 和 oppsite partity
                // WORKAROUND : 
                //              1) 是不是直接不支持场编码问题就都解决了, 当前 h264 场编码已经应用得比较少了;
                //                 支持场编码一方面逻辑异常复杂,另一方面也不好进行测试验证
                //              2) 确认一下 FFmpeg 6.x (FFmpeg/libavcodec/h264_refs.c) 这部分的代码逻辑 (,但是看起来不是很好确认 ...) 
                if (_picture->referenceFlag & H264PictureContext::used_for_short_term_reference)
                {
                    if (_picture->bottom_field_flag)
                    {
                        _picture->PicNum = 2 * _picture->FrameNumWrap + 1; // (8-30)
                    }
                    else
                    {
                        _picture->PicNum = 2 * _picture->FrameNumWrap; // (8-31)
                    }
                }
                if (_picture->referenceFlag & H264PictureContext::used_for_long_term_reference)
                {
                    if (_picture->bottom_field_flag)
                    {
                        _picture->LongTermPicNum = 2 * _picture->LongTermFrameIdx + 1;
                    }
                    else
                    {
                        _picture->LongTermPicNum = 2 * picture->LongTermFrameIdx;
                    }
                }
            }
        }
    }
}

/**
 * @sa  ISO 14496/10(2020) - 8.2.4.2 Initialization process for reference picture lists
 */
void H264SliceDecodingProcess::InitializationProcessForReferencePictureLists(H264SliceHeaderSyntax::ptr slice, H264PictureContext::cache pictures)
{
    {
        _RefPicList0.clear();
        _RefPicList1.clear();
    }
    // 8.2.4.2.1 Initialization process for the reference picture list for P and SP slices in frames
    if ((slice->slice_type == H264SliceType::MMP_H264_P_SLICE || slice->slice_type == H264SliceType::MMP_H264_SP_SLICE) && slice->field_pic_flag == 0)
    {
        std::vector<uint64_t> shortTermRefPicList; // the highest PicNum value in descending order
        std::vector<uint64_t> longTermRefList;     // the lowest LongTermPicNum value in ascending order
        for (auto picture : pictures)
        {
            if (picture->referenceFlag & H264PictureContext::used_for_short_term_reference)
            {
                shortTermRefPicList.push_back(picture->PicNum);
            }
            if (picture->referenceFlag & H264PictureContext::used_for_long_term_reference)
            {
                longTermRefList.push_back(picture->LongTermPicNum);
            }
            std::sort(shortTermRefPicList.begin(), shortTermRefPicList.end());
            std::reverse(shortTermRefPicList.begin(), shortTermRefPicList.end());
            std::sort(longTermRefList.begin(), longTermRefList.end());
            for (auto& PicNum : shortTermRefPicList)
            {
                _RefPicList0.push_back(PicNum);
            }
            for (auto& LongTermPicNum : longTermRefList)
            {
                _RefPicList0.push_back(LongTermPicNum);
            }
        }
    }
    // 8.2.4.2.2 Initialization process for the reference picture list for P and SP slices in fields
    else if ((slice->slice_type == H264SliceType::MMP_H264_P_SLICE || slice->slice_type == H264SliceType::MMP_H264_SP_SLICE) && slice->field_pic_flag == 1)
    {
        // Hint : not support filed for now
        assert(false);
    }
    else if ((slice->slice_type == H264SliceType::MMP_H264_B_SLICE) && slice->field_pic_flag == 0)
    {
        // Hint : not support for now
    }
    else if ((slice->slice_type == H264SliceType::MMP_H264_B_SLICE) && slice->field_pic_flag == 1)
    {
        // Hint : not support for now
        assert(false);
    }
    // TODO
    {
        _RefPicList0.resize(std::min((size_t)slice-> num_ref_idx_l0_active_minus1 + 1, _RefPicList0.size()));
        _RefPicList1.resize(std::min((size_t)slice-> num_ref_idx_l1_active_minus1 + 1, _RefPicList1.size()));
    }
}

/**
 * @sa  ISO 14496/10(2020) - 8.2.4.3 Modification process for reference picture lists
 */
void H264SliceDecodingProcess::ModificationProcessForReferencePictureLists(H264SliceHeaderSyntax::ptr slice, H264SpsSyntax::ptr sps, H264PictureContext::cache pictures, H264PictureContext::ptr picture)
{
    uint64_t MaxPicNum = 0;
    uint64_t CurrPicNum = 0;
    uint64_t picNumLX = 0;
    // determine CurrPicNum
    {
        if (slice->field_pic_flag == 0)
        {
            CurrPicNum = slice->frame_num;
        }
        else if (slice->field_pic_flag == 1)
        {
            CurrPicNum = 2 * slice->frame_num + 1;
        }
    }
    // determine MaxPicNum
    {
        uint64_t MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4);
        if (slice->field_pic_flag == 0)
        {
            MaxPicNum = MaxFrameNum;
        }
        else if (slice->field_pic_flag == 1)
        {
            MaxPicNum = 2 * MaxFrameNum;
        }
    }

    auto modificationProcessForReferencePictureLists = [&](uint32_t refIdxLX, std::vector<uint64_t>& RefPicListX, uint32_t num_ref_idx_lX_active_minus1)
    {
        for (const auto& modification_of_pic_nums_idc : slice->rplm->modification_of_pic_nums_idcs)
        {
            // 8.2.4.3.1 Modification process of reference picture lists for short-term reference pictures
            if (modification_of_pic_nums_idc == 0 || modification_of_pic_nums_idc == 1)
            {
                uint64_t picNumLXNoWrap = CurrPicNum;
                uint64_t picNumLXPred = picNumLXNoWrap;
                // determine picNumLXNoWrap
                {
                    if (modification_of_pic_nums_idc == 0) // (8-34)
                    {
                        if (picNumLXNoWrap - (slice->rplm->abs_diff_pic_num_minus1 + 1) < 0)
                        {
                            picNumLXNoWrap = picNumLXPred - (slice->rplm->abs_diff_pic_num_minus1 + 1) + MaxPicNum;
                        }
                        else
                        {
                            picNumLXNoWrap = picNumLXPred - (slice->rplm->abs_diff_pic_num_minus1 + 1);
                        }
                    }
                    else if (modification_of_pic_nums_idc == 1) // (8-35)
                    {
                        if (picNumLXPred + (slice->rplm->abs_diff_pic_num_minus1 + 1) >= MaxPicNum)
                        {
                            picNumLXNoWrap = picNumLXPred + (slice->rplm->abs_diff_pic_num_minus1 + 1) - MaxPicNum;
                        }
                        else
                        {
                            picNumLXNoWrap = picNumLXPred + (slice->rplm->abs_diff_pic_num_minus1 + 1);
                        }
                    }
                }
                picNumLXNoWrap = picNumLXPred;
                // determine picNumLX
                {
                    if (picNumLXNoWrap > CurrPicNum)
                    {
                        picNumLX = picNumLXNoWrap - MaxPicNum;
                    }
                    else
                    {
                        picNumLX = picNumLXNoWrap;
                    }
                }
                // (8-37)
                {
                    auto PicNumF = [pictures, MaxPicNum](uint32_t cIdx) -> uint32_t
                    {
                        for (auto _picture : pictures)
                        {
                            if (_picture->PicNum == cIdx)
                            {
                                if (_picture->referenceFlag & H264PictureContext::used_for_short_term_reference)
                                {
                                    return cIdx;
                                }
                            }
                        }
                        return MaxPicNum;
                    };
                    for (uint32_t cIdx = num_ref_idx_lX_active_minus1+1; cIdx > refIdxLX; cIdx--)
                    {
                        RefPicListX[cIdx] = RefPicListX[cIdx-1];
                    }
                    RefPicListX[refIdxLX++] = picNumLX;
                    uint32_t nIdx = refIdxLX;
                    for (uint32_t cIdx = refIdxLX; cIdx <= num_ref_idx_lX_active_minus1+1; cIdx++)
                    {
                        if (PicNumF(RefPicListX[cIdx]) != picNumLX)
                        {
                            RefPicListX[nIdx++] = RefPicListX[cIdx];
                        }
                    }
                }
            }
            // 8.2.4.3.2 Modification process of reference picture lists for long-term reference pictures
            else if (modification_of_pic_nums_idc == 2)
            {
                auto LongTermPicNumF = [pictures, MaxPicNum, picture](uint32_t cIdx) -> uint32_t
                {
                    for (auto _picture : pictures)
                    {
                        if (_picture->LongTermFrameIdx == cIdx)
                        {
                            if (_picture->referenceFlag & H264PictureContext::used_for_long_term_reference)
                            {
                                return cIdx;
                            }
                        }
                    }
                    return 2 * (picture->MaxLongTermFrameIdx + 1);
                };

                for (uint32_t cIdx = num_ref_idx_lX_active_minus1 + 1; cIdx > refIdxLX; cIdx--)
                {
                    RefPicListX[cIdx] = RefPicListX[cIdx - 1];
                }
                RefPicListX[refIdxLX++] = slice->rplm->long_term_pic_num;
                uint32_t nIdx = refIdxLX;
                for (uint32_t cIdx = refIdxLX; cIdx <= num_ref_idx_lX_active_minus1 + 1; cIdx++)
                {
                    if (LongTermPicNumF(RefPicListX[cIdx]) != slice->rplm->long_term_pic_num)
                    {
                        RefPicListX[nIdx++] = RefPicListX[cIdx];
                    }
                }
            }
            else if (modification_of_pic_nums_idc == 3)
            {
                break;
            }
        }
    };

    if (slice->rplm->ref_pic_list_modification_flag_l0)
    {
        uint32_t refIdxL0 = 0;
        modificationProcessForReferencePictureLists(refIdxL0, _RefPicList0, slice->num_ref_idx_l0_active_minus1);
    }
    if (slice->rplm->ref_pic_list_modification_flag_l1 && slice->slice_type == H264SliceType::MMP_H264_B_SLICE)
    {
        uint32_t refIdxL1 = 0;
        modificationProcessForReferencePictureLists(refIdxL1, _RefPicList1, slice->num_ref_idx_l1_active_minus1);
    }
}

/*************************************** 8.2.4 Decoding process for reference picture lists construction(End) ******************************************/

/*************************************** 8.2.5 Decoded reference picture marking process(Begin) ******************************************/

/**
 * @sa ISO 14496/10(2020) - 8.2.5 Decoded reference picture marking process
 */
void H264SliceDecodingProcess::DecodeReferencePictureMarkingProcess(H264SliceHeaderSyntax::ptr slice, H264SpsSyntax::ptr sps, H264PictureContext::cache pictures, H264PictureContext::ptr picture, uint8_t nal_ref_idc)
{
    // Hint : A decoded picture with nal_ref_idc not equal to 0, referred to as a reference picture, is marked as "used for short-term reference" or "used for long-term reference".
    //        - decoded reference frame : both of its fields are marked the same as the frame
    //        - complementary reference field pair : the pair is marked the same as both of its fields
    if (nal_ref_idc == 0)
    {
        return;
    }
    if (slice->slice_type != H264SliceType::MMP_H264_I_SLICE)
    {
        DecodingProcessForPictureNumbers(slice, sps, pictures, picture);
    }
    SequenceOfOperationsForDecodedReferencePictureMarkingProcess(slice, sps, pictures, picture);
}

/**
 * @sa ISO 14496/10(2020) - 8.2.5.1 Sequence of operations for decoded reference picture marking process 
 */
void H264SliceDecodingProcess::SequenceOfOperationsForDecodedReferencePictureMarkingProcess(H264SliceHeaderSyntax::ptr slice, H264SpsSyntax::ptr sps, H264PictureContext::cache pictures, H264PictureContext::ptr picture)
{
    if (slice->slice_type == H264SliceType::MMP_H264_I_SLICE)
    {
        pictures.clear();
        if (slice->drpm->long_term_reference_flag == 0)
        {
            picture->referenceFlag = H264PictureContext::used_for_short_term_reference;
            picture->MaxLongTermFrameIdx = no_long_term_frame_indices;
        }
        else if (slice->drpm->long_term_reference_flag == 1)
        {
            picture->referenceFlag = H264PictureContext::used_for_long_term_reference;
            picture->LongTermFrameIdx = 0;
            picture->MaxLongTermFrameIdx = 0;
        }
    }
    else
    {
        if (slice->drpm->adaptive_ref_pic_marking_mode_flag == 0)
        {
            SlidingWindowDecodedReferencePictureMarkingProcess(slice, sps, pictures, picture);
        }
        else
        {
            AdaptiveMemoryControlDecodedReferencePicutreMarkingPorcess(slice, pictures, picture);
        }
    }
    if (slice->slice_type != H264SliceType::MMP_H264_I_SLICE && !(picture->unused_for_reference & H264PictureContext::used_for_long_term_reference))
    {
        picture->referenceFlag = H264PictureContext::used_for_short_term_reference;
    }
}

/**
 * @sa ISO 14496/10(2020) - 8.2.5.2 Decoding process for gaps in frame_num
 */
void H264SliceDecodingProcess::DecodingProcessForGapsInFrameNum(H264SliceHeaderSyntax::ptr slice, H264SpsSyntax::ptr sps, H264PictureContext::ptr picture, uint64_t PrevRefFrameNum)
{

    uint64_t MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4); // (7-10)
    if (!(slice->frame_num != PrevRefFrameNum && slice->frame_num != (PrevRefFrameNum + 1) % MaxFrameNum))
    {
        return;
    }
    assert(false);
    // Hint : no support for now
}

/**
 * @sa ISO 14496/10(2020) - 8.2.5.3 Sliding window decoded reference picture marking process
 */
void H264SliceDecodingProcess::SlidingWindowDecodedReferencePictureMarkingProcess(H264SliceHeaderSyntax::ptr slice, H264SpsSyntax::ptr sps, H264PictureContext::cache pictures, H264PictureContext::ptr picture)
{
    if (PictureIsSecondField(picture))
    {
        H264PictureContext::ptr compPicture = FindComplementaryPicture(pictures, picture);
        if (!compPicture)
        {
            return;
        }
        if (compPicture->referenceFlag & H264PictureContext::used_for_short_term_reference)
        {
            picture->referenceFlag |= H264PictureContext::used_for_short_term_reference;
        }
    }
    else
    {
        uint32_t numShortTerm = 0, numLongTerm = 0;
        for (auto& _picture : pictures)
        {
            if (_picture->referenceFlag & H264PictureContext::used_for_short_term_reference)
            {
                numShortTerm++;
            }
            if (_picture->referenceFlag & H264PictureContext::used_for_long_term_reference)
            {
                numLongTerm++;
            }
        }
        if (numShortTerm + numLongTerm == std::max(1u, sps->max_num_ref_frames))
        {
            H264PictureContext::ptr __picture = nullptr;
            uint64_t minFrameNumWrap = UINT64_MAX;
            for (auto& _picture : pictures)
            {
                if (_picture->FrameNumWrap < minFrameNumWrap)
                {
                    minFrameNumWrap = _picture->FrameNumWrap;
                    __picture = _picture;
                    break;
                }
            }
            __picture->referenceFlag = H264PictureContext::unused_for_reference;
            if (__picture->field_pic_flag == 1)
            {
                H264PictureContext::ptr compPicture = nullptr;
                compPicture = FindComplementaryPicture(pictures, __picture);
                if (!compPicture)
                {
                    assert(false);
                    return;
                }
                compPicture->referenceFlag = H264PictureContext::unused_for_reference;
            }     
        }
    }

}

/**
 * @sa ISO 14496/10(2020) - 8.2.5.4 Adaptive memory control decoded reference picture marking process 
 */
void H264SliceDecodingProcess::AdaptiveMemoryControlDecodedReferencePicutreMarkingPorcess(H264SliceHeaderSyntax::ptr slice, H264PictureContext::cache pictures, H264PictureContext::ptr picture)
{
    for (const auto& memory_management_control_operation : slice->drpm->memory_management_control_operations)
    {
        switch (memory_management_control_operation)
        {
            // See also : 8.2.5.4.1 Marking process of a short-term reference picture as "unused for reference"
            case H264MmcoType::MMP_H264_MMOO_1: /* unmark short term reference */
            {
                uint64_t picNumX = GetPicNumX(slice);
                UnMarkUsedForShortTermReference(pictures, picNumX);
                break;
            }
            // See also : 8.2.5.4.2 Marking process of a long-term reference picture as "unused for reference"
            case H264MmcoType::MMP_H264_MMOO_2: /* unmark long term reference */
            {
                UnMarkUsedForLongTermReference(pictures, slice->drpm->long_term_pic_num);
                break;
            }
            // See also : 8.2.5.4.3 Assignment process of a LongTermFrameIdx to a short-term reference picture
            case H264MmcoType::MMP_H264_MMOO_3: /* short term reference to long term reference */
            {
                uint64_t picNumX = GetPicNumX(slice);
                UnMarkUsedForReference(pictures, picture->long_term_frame_idx);
                MarkShortTermReferenceToLongTermReference(pictures, picNumX, slice->drpm->long_term_frame_idx);
                break;
            }
            // See also : 8.2.5.4.4 Decoding process for MaxLongTermFrameIdx
            case H264MmcoType::MMP_H264_MMOO_4:
            {
                int64_t MaxLongTermFrameIdx = slice->drpm->max_long_term_frame_idx_plus1 == 0 ? no_long_term_frame_indices : slice->drpm->max_long_term_frame_idx_plus1 - 1;
                for (auto _picture : pictures)
                {
                    if (_picture->LongTermFrameIdx > slice->drpm->max_long_term_frame_idx_plus1 - 1)
                    {
                        if (_picture->referenceFlag & H264PictureContext::used_for_long_term_reference)
                        {
                            _picture->referenceFlag = H264PictureContext::unused_for_reference;
                            _picture->MaxLongTermFrameIdx = MaxLongTermFrameIdx;
                        }
                    }
                }
                break;
            }
            // See also : 8.2.5.4.5 Marking process of all reference pictures as "unused for reference" and setting
            //            MaxLongTermFrameIdx to "no long-term frame indices"
            case H264MmcoType::MMP_H264_MMOO_5:
            {
                for (auto _picture : pictures)
                {
                    _picture->MaxLongTermFrameIdx = no_long_term_frame_indices;
                    _picture->referenceFlag = H264PictureContext::unused_for_reference;
                }
                break;
            }
            // See also : 8.2.5.4.6 Process for assigning a long-term frame index to the current picture
            case H264MmcoType::MMP_H264_MMOO_6:
            {
                for (auto _picture : pictures)
                {
                    if (_picture->LongTermFrameIdx >= slice->drpm->long_term_frame_idx)
                    {
                        _picture->referenceFlag = H264PictureContext::unused_for_reference;
                        H264PictureContext::ptr compPicture = FindComplementaryPicture(pictures, _picture);
                        if (compPicture)
                        {
                            compPicture->referenceFlag = H264PictureContext::unused_for_reference;
                        }
                    }
                }
                {
                    picture->referenceFlag |= H264PictureContext::used_for_long_term_reference;
                    picture->LongTermFrameIdx = picture->long_term_frame_idx;
                    H264PictureContext::ptr compPicture = FindComplementaryPicture(pictures, picture);
                    if (compPicture)
                    {
                        compPicture->referenceFlag |= H264PictureContext::used_for_long_term_reference;
                        compPicture->LongTermFrameIdx = picture->long_term_frame_idx;
                    }
                }
                break;
            }
            default:
                break;
        }
    }
}

/*************************************** 8.2.5 Decoded reference picture marking process(End) ******************************************/

void H264SliceDecodingProcess::SliceDecodingProcess(H264NalSyntax::ptr nal)
{
    switch (nal->nal_unit_type)
    {
        case H264NaluType::MMP_H264_NALU_TYPE_SPS:
        {
            _spss[nal->sps->seq_parameter_set_id] = nal->sps;
            break;
        }
        case H264NaluType::MMP_H264_NALU_TYPE_PPS:
        {
            _ppss[nal->pps->pic_parameter_set_id] = nal->pps;
            break;
        }
        case H264NaluType::MMP_H264_NALU_TYPE_SLICE:
        {
            H264PictureContext::ptr picture = std::make_shared<H264PictureContext>();
            H264SpsSyntax::ptr sps = nullptr;
            H264PpsSyntax::ptr pps = nullptr;
            if (!_ppss.count(nal->slice->pic_parameter_set_id))
            {
                break;
            }
            pps = _ppss[nal->slice->pic_parameter_set_id];
            if (!_spss.count(pps->seq_parameter_set_id))
            {
                break;
            }
            sps = _spss[pps->seq_parameter_set_id];
            {
                picture->field_pic_flag = nal->slice->field_pic_flag;
                picture->bottom_field_flag = nal->slice->bottom_field_flag;
                picture->pic_order_cnt_lsb = nal->slice->pic_order_cnt_lsb;
                picture->long_term_frame_idx = nal->slice->drpm->long_term_frame_idx;
            }
            OnDecodingBegin();
            DecodingProcessForPictureOrderCount(sps, nal->slice, nal->nal_ref_idc, picture);
            if (nal->slice->slice_type == H264SliceType::MMP_H264_P_SLICE ||
                nal->slice->slice_type == H264SliceType::MMP_H264_SP_SLICE ||
                nal->slice->slice_type == H264SliceType::MMP_H264_B_SLICE
            )
            {
                DecodingProcessForReferencePictureListsConstruction(nal->slice, sps, _pictures, picture);
            }
            OnDecodingEnd();
            _pictures.insert(picture);
            _prevPicture = picture;
            break;
        }
        default:
            break;
    }
}

void H264SliceDecodingProcess::OnDecodingBegin()
{
    for (auto& task : _beginTasks)
    {
        task();
    }
    _beginTasks.clear();
}

void H264SliceDecodingProcess::OnDecodingEnd()
{
    for (auto& task : _endTasks)
    {
        task();
    }
    _endTasks.clear();
    H264PictureContext::cache pictures;
    for (auto picture : _pictures)
    {
        if (picture->referenceFlag & H264PictureContext::used_for_short_term_reference || picture->referenceFlag & H264PictureContext::used_for_long_term_reference)
        {
            pictures.insert(picture);
        }
    }
    _pictures.swap(pictures);
}

} // namespace Codec
} // namespace Mmp