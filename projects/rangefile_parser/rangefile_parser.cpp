﻿// rangefile_parser.cpp: 定义应用程序的入口点。
//

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include "include/crc32.h"
#include "include/rangefile_parser.h"
#include "include/unicore.h"

/**
    Baserange size: 36 + 44 * M，when M = 130，5756 Bytes

    举个例子，当前帧一共有332有效字节，除去（头28字节，observation_num4字节），还剩下300字节。
    300 / 44 = 6
    300 % 44 = 36
        curr_baserange.range_data[0]
        curr_baserange.range_data[1]
        ...
        curr_baserange.range_data[5]
        curr_baserange.range_data[6]
            byte0
            byte1
            ...
            byte35

        Step1: 用 combined_buf 将 curr_baserange.range_data[6]填充完毕
        Step2: 还剩下 23个range_data，额外还有4字节
        Step3: 填充
            curr_baserange.range_data[7]
            curr_baserange.range_data[8]
            ...
            curr_baserange.range_data[29]
            curr_baserange.range_data[30]
                byte0
                byte1
                ...
                byte[3]

            6 + 23 = 29
            

**/


using namespace std;

#define RX_BUF_SIZE  1024U

#define BUFFERSIZE 1024U
#define DATASIZE 1024U
#define ELEMENTCOUNT 1
#define ELEMENTSIZE (DATASIZE/ELEMENTCOUNT)


#define DEBUG_LINE_START    715
#define DEBUG_LINE_END    722

#define WEEK_BASE    2246
#define SECONDS_IN_ONE_WEEK    604800

uint8_t data_buf_for_crc[MAX_DATA_BUF_LEN] = {'\0'};
/* BEGIN baserange */
RangeMeasurements curr_baserange;
/* BEGIN bestsats */
UNCBESTSATSMsg curr_bestsats;
/* BEGIN rangecmp */
CompressedRangeMesaurements curr_rangecmp;
/* BEGIN bestpos */
BESTPOSData m_bestpos_data;

typedef enum {
    LAST_BYTES_0 = 0,
    LAST_BYTES_1 = 1,
    LAST_BYTES_2 = 2,
    LAST_BYTES_3 = 3,
    LAST_BYTES_4 = 4,
    LAST_BYTES_5 = 5,
    LAST_BYTES_BIG = 6,        // 说明上一个ELEMENT（1024字节）至少还有6个字节还没解析。
} LAST_BYTES_NUM;

typedef struct {
    int last_loop_cnt;
    LAST_BYTES_NUM last_bytes_num;
    uint16_t big_len;
    uint8_t msg_id_lo_byte;
} LAST_FRAME_RECORD;

typedef enum {
    PARSING_INIT = 0,
    FIND_HEADERSYNC_FROM_LAST_ELEMENT = 1,
    FIND_HEADERSYNC_FROM_CURR_ELEMENT = 2,
    FOUND_HEADERSYNC = 3,
    FOUND_MSG_ID = 4,                // 找到帧头的标准是检测到了 "0xAA 0x44 0x12 0x1C"
    FIND_HEADER = 5,
    FOUND_HEADER = 6,
    FIND_ITEM_NUM = 7,
    FOUND_ITEM_NUM = 8,
    NEED_PARSE_BESTPOS_BODY = 9,
    NEED_PARSE_BASERANGE_BODY = 10,
    NEED_PARSE_BESTSATS_BODY = 11,
    NEED_PARSE_RANGECMP_BODY = 12,
    PARSE_BESTPOS_DONE = 13,
    PARSE_BASERANGE_DONE = 14,        // 表明拿到了baserange这一帧的全部数据，但是还没做CRC校验。
    PARSE_BESTSATS_DONE = 15,
    PARSE_RANGECMP_DONE = 16,
    PARSE_STATE_NUM = 17,
} PARSING_STATE;

typedef struct {
    PARSING_STATE parsing_state;
    uint16_t msg_id;
    uint32_t total_frame_len;        // total_frame_len = had_parsed_len + need_parse_len
    uint32_t had_parsed_len;
    uint32_t need_parse_len;
    uint32_t had_parsed_item_num;
    uint32_t left_size_in_last_element;        // 上一个ELEMENT数据（1024字节）的尾巴
    uint16_t left_size_in_curr_element;        // 这一个ELEMENT数据（1024字节）减去“与上一个ELEMENT尾巴拼凑的一个item”后剩余的大小
    uint16_t left_item_num_in_curr_element;    // left_size_in_curr_element包含多少个item
    LAST_FRAME_RECORD last_frame_info;
    bool need_rd_new_data;
} PARSING_INFO;

PARSING_INFO m_parsing_info;

typedef struct {
    uint32_t baserange_found_frame_cnt = 0;
    uint32_t baserange_verified_frame_cnt = 0;
    uint32_t bestpos_found_frame_cnt = 0;
    uint32_t bestpos_verified_frame_cnt = 0;
    uint32_t rangecmp_found_frame_cnt = 0;
    uint32_t rangecmp_verified_frame_cnt = 0;
    uint32_t bestsats_found_frame_cnt = 0;
    uint32_t bestsats_verified_frame_cnt = 0;
} RTK_STATISTICS_INFO;

RTK_STATISTICS_INFO m_rtk_statistics_info;

uint8_t getSignalType(ChannelStatus* chnl_status) {
    uint8_t ret = 0xff;
    uint32_t sat_sys = chnl_status->satellite_sys;
    uint32_t sig_type = chnl_status->signal_type;

    switch (sat_sys)
    {
        case SatSys_GPS: {
            if (GPS_L1CA == sig_type)
            {
                ret = 1;
            }
            else if (GPS_L2P_W == sig_type || GPS_L2C == sig_type || GPS_L2P == sig_type)
            {
                ret = 2;
            }
            break;
        }
        case SatSys_QZSS: {
            if (QZSS_L1C == sig_type)
            {
                ret = 1;
            }
            else if (QZSS_L2P == sig_type || QZSS_L2C == sig_type)
            {
                ret = 2;
            }
            break;
        }
        case SatSys_GLONASS: {
            if (GLO_L1C == sig_type)
            {
                ret = 1;
            }
            else if (GLO_L2C == sig_type)
            {
                ret = 2;
            }
            break;
        }
        case SatSys_SBAS: {
            if (SBAS_L1CA == sig_type)
            {
                ret = 1;
            }
            break;
        }
        case SatSys_GAL: {
            if (GAL_E1B == sig_type || GAL_E1C == sig_type)
            {
                ret = 1;
            }
            else if (GAL_E5BQ == sig_type)
            {
                ret = 2;
            }
            break;
        }
        case SatSys_BDS: {
            if (BDS_B1I == sig_type || BDS_B1Q == sig_type || BDS_B1CQ == sig_type || BDS_B1C_DATA == sig_type)
            {
                ret = 1;
            }
            else if (BDS_B2Q == sig_type || BDS_B2AQ == sig_type || BDS_B2B == sig_type || BDS_B2I == sig_type || BDS_B2A_DATA == sig_type)
            {
                ret = 2;
            }
            break;
        }
        default:
        {
            ret = 0xff;
            break;
        }
    }

    return ret;
}


void statisticNumberOfSatellitesPerSignal(UNCBESTSATSMsg *bestsats_data)
{
    uint32_t i;
    uint32_t satellites_of_l1 = 0;
    uint32_t satellites_of_l2 = 0;

    for (i = 0; i < bestsats_data->num_of_sats; i++)
    {
        if (((bestsats_data->sats_data[i].sat_signal_mask >> 4) & 0x01) == 0x01)    // 检查共视卫星
        {
            if ((bestsats_data->sats_data[i].sat_signal_mask & 0x01) == 0x01)
            {
                satellites_of_l1++;
            }
            if (((bestsats_data->sats_data[i].sat_signal_mask >> 1) & 0x01) == 0x01)
            {
                satellites_of_l2++;
            }
        }
    }

    int bestsats_wn = bestsats_data->hdr_msg.week;
    int bestsats_wn_converted = bestsats_wn - WEEK_BASE;
    if (bestsats_wn_converted < 0)
    {
        bestsats_wn_converted = 0;
    }
    float bestsats_time_in_sec = (bestsats_wn_converted * SECONDS_IN_ONE_WEEK) + bestsats_data->hdr_msg.ms / 1000.0f;

    printf("%s(%d) bestsats time: %f, satellites_of_l1: %d, satellites_of_l2: %d\r\n", __func__, __LINE__, bestsats_time_in_sec, satellites_of_l1, satellites_of_l2);
}

bool checkCarSatelliteInBestSatellite(UNCBESTSATSMsg* bestsats_data, uint32_t satellite_sys, uint32_t satellite_prn, uint32_t sig_type)
{
    uint32_t i;
    bool ret = false;
    for (i = 0; i < curr_bestsats.num_of_sats; i++)
    {
        uint32_t sat_prn = bestsats_data->sats_data[i].sat_prn;
        uint16_t bestsats_prn = ((uint8_t*)(&sat_prn))[2] | (((uint8_t*)(&sat_prn))[3] << 8);
        if ((satellite_sys == bestsats_data->sats_data[i].sat_sys) && (satellite_prn == bestsats_prn))
        {

            if (((bestsats_data->sats_data[i].sat_signal_mask >> 4) & 0x01) == 0x01)    // 检查共视卫星
            {
                if ((bestsats_data->sats_data[i].sat_signal_mask & 0x01) == 0x01)        // 检查是否匹配L1信号类型
                {
                    switch (satellite_sys)
                    {
                    case SatSys_GPS:
                    {
                        if (GPS_L1CA == sig_type)
                        {
                            ret = true;
                        }
                        break;
                    }
                    case SatSys_QZSS:
                    {
                        if (QZSS_L1C == sig_type)
                        {
                            ret = true;
                        }
                        break;
                    }
                    case SatSys_GLONASS:
                    {
                        if (GLO_L1C == sig_type)
                        {
                            ret = true;
                        }
                        break;
                    }
                    case SatSys_SBAS:
                    {
                        if (SBAS_L1CA == sig_type)
                        {
                            ret = true;
                        }
                        break;
                    }
                    case SatSys_GAL:
                    {

                        if (GAL_E1B == sig_type || GAL_E1C == sig_type || GAL_E5AQ == sig_type)
                        {
                            ret = true;
                        }
                        break;
                    }

                    case SatSys_BDS: {
                        if (BDS_B1I == sig_type || BDS_B1Q == sig_type || BDS_B1CQ == sig_type || BDS_B1C_DATA == sig_type)
                        {
                            ret = 1;
                        }
                        break;
                    }

                    default:
                    {
                        break;
                    }
                    }
                }

                if (((bestsats_data->sats_data[i].sat_signal_mask >> 1) & 0x01) == 0x01)        // 检查是否匹配L2信号类型
                {
                    switch (satellite_sys)
                    {
                        case SatSys_GPS:
                        {
                            if (GPS_L2P_W == sig_type || GPS_L2C == sig_type || GPS_L2P == sig_type)
                            {
                                ret = true;
                            }
                            break;
                        }
                        case SatSys_QZSS:
                        {
                            if (QZSS_L2P == sig_type || QZSS_L2C == sig_type)
                            {
                                ret = true;
                            }
                            break;
                        }
                        case SatSys_GLONASS:
                        {
                            if (GLO_L2C == sig_type)
                            {
                                ret = true;
                            }
                            break;
                        }
                        case SatSys_SBAS:
                        {
                            break;
                        }
                        case SatSys_GAL:
                        {
                            break;
                        }
                        case SatSys_BDS:
                        {
                            if (BDS_B2Q == sig_type || BDS_B2AQ == sig_type || BDS_B2B == sig_type || BDS_B2I == sig_type || BDS_B2A_DATA == sig_type)
                            {
                                ret = true;
                            }
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }    // end-of 'switch'
                }
            }    // end-of “检查共视卫星”
        }
    }    // end-of 'for' loop

    return ret;
}

void statisticsCarSatelliteCN0(CompressedRangeMesaurements *range_cmp_data)
{
    uint32_t i;
    float top4_cn0[4] = { 0.0, 0.0, 0.0, 0.0 };    // 从小到大排序
    float acc_cn0 = 0.0;
    uint32_t acc_cn0_count = 0;
    for (int i = 0; i < range_cmp_data->observation_num; i++)
    {
        if (!checkCarSatelliteInBestSatellite(&curr_bestsats, range_cmp_data->range_data[i].channel_status.satellite_sys, \
            range_cmp_data->range_data[i].range_record.satellite_prn, range_cmp_data->range_data[i].channel_status.signal_type))
        {
            continue;
        }

        // 能够走到这里，说明这颗卫星是共视卫星，要么是L1频点，要么是L2频点
        if ((float)(range_cmp_data->range_data[i].range_record.locktime) / 32.0f < 5)
        {
            continue;
        }

        // 选取信号最强的4颗卫星，并且求所有卫星信号总和
        acc_cn0 += range_cmp_data->range_data[i].range_record.carrier_to_noise;
        acc_cn0_count++;

        int32_t m, n;
        float tmp1, tmp2;
        for (m = 3; m >= 0; m--)
        {
            if (range_cmp_data->range_data[i].range_record.carrier_to_noise >= top4_cn0[m])
            {
                tmp1 = top4_cn0[m];
                top4_cn0[m] = (float)(range_cmp_data->range_data[i].range_record.carrier_to_noise);
                for (n = m-1; n >= 0; n--)
                {
                    tmp2 = top4_cn0[n];
                    top4_cn0[n] = tmp1;
                    tmp1 = tmp2;
                }
                break;
            }
        }
    }    // end-of 'for'

    // 求平均
    float top4_mean = 0.0f;
    float acc_mean = 0.0f;
    if (acc_cn0_count > 0)
    {
        top4_mean = ((top4_cn0[0] + top4_cn0[1] + top4_cn0[2] + top4_cn0[3]) / 4) + 20;
        acc_mean = acc_cn0 / acc_cn0_count + 20;
    }

    signal_quality_e signal_quality;
    if (top4_mean >= 45.5f && acc_mean >= 42.0f)
    {
        signal_quality = SIGNAL_QUALITY_GOOD;
    }
    else if (top4_mean >= 44.25f && acc_mean > 38.2f)
    {
        signal_quality = SIGNAL_QUALITY_NORMAL;
    }
    else if (top4_mean == 0 || acc_mean == 0)
    {
        signal_quality = SIGNAL_QUALITY_NONE;
    }
    else
    {
        signal_quality = SIGNAL_QUALITY_BAD;
    }
    int rangecmp_wn = range_cmp_data->cmp_range_header.week;
    int rangecmp_wn_converted = rangecmp_wn - WEEK_BASE;
    if (rangecmp_wn_converted < 0)
    {
        rangecmp_wn_converted = 0;
    }
    float rangecmp_time_in_sec = (rangecmp_wn_converted * SECONDS_IN_ONE_WEEK) + range_cmp_data->cmp_range_header.ms / 1000.0f;

    printf("%s(%d)  range_cmp time: %f, acc_cn0_count: %d, top4_mean: %f, acc_mean: %f, sig_qual: %d\r\n", __func__, __LINE__, rangecmp_time_in_sec, acc_cn0_count, top4_mean, acc_mean, signal_quality);
}

void statisticsBaseStationSatellitesCN0(RangeMeasurements *baserange_data)
{
    float top4_cn0[4] = { 0.0, 0.0, 0.0, 0.0 };    // 从小到大顺序
    float acc_cn0 = 0.0;
    uint32_t acc_cn0_count = 0;

    uint32_t satellites_of_l1 = 0;
    uint32_t satellites_of_l2 = 0;
    for (int i = 0; i < baserange_data->observation_num; i++) {
        printf("LINE%d, range_data[%d], locktime: %f, CN0: %d\r\n", __LINE__, i, baserange_data->range_data[i].locktime, baserange_data->range_data[i].carrier_to_noise);
        if (baserange_data->range_data[i].locktime < 5 || baserange_data->range_data[i].carrier_to_noise < 30) {
            continue;
        }

        uint8_t signal_type = getSignalType(&(baserange_data->range_data[i].channel_status));
        if (1 == signal_type) {
            satellites_of_l1++;
        }
        else if (2 == signal_type) {
            satellites_of_l2++;
        }

        // 选取CN0最高的4个item，并且求所有items总的CN0
        acc_cn0 += baserange_data->range_data[i].carrier_to_noise;
        acc_cn0_count++;

        int32_t m, n;
        float tmp1, tmp2;
        for (m = 3; m >= 0; m--)
        {
            if (baserange_data->range_data[i].carrier_to_noise >= top4_cn0[m])
            {
                tmp1 = top4_cn0[m];
                top4_cn0[m] = baserange_data->range_data[i].carrier_to_noise;
                for (n = m-1; n >= 0; n--)
                {
                    tmp2 = top4_cn0[n];
                    top4_cn0[n] = tmp1;
                    tmp1 = tmp2;
                }
                break;
            }
        }
    }

    // 求平均
    float top4_mean = 0.0f;
    float acc_mean = 0.0f;
    if (acc_cn0_count > 0)
    {
        top4_mean = (top4_cn0[0] + top4_cn0[1] + top4_cn0[2] + top4_cn0[3]) / 4;
        acc_mean = acc_cn0 / acc_cn0_count;
    }

    signal_quality_e signal_quality;
    if (top4_mean >= 45.5f && acc_mean >= 42.0f)
    {
        signal_quality = SIGNAL_QUALITY_GOOD;
    }
    else if (top4_mean >= 44.25f && acc_mean > 38.2f)
    {
        signal_quality = SIGNAL_QUALITY_NORMAL;
    }
    else if (top4_mean == 0 || acc_mean == 0)
    {
        signal_quality = SIGNAL_QUALITY_NONE;
    }
    else
    {
        signal_quality = SIGNAL_QUALITY_BAD;
    }

    int baserange_wn = baserange_data->range_header.week;
    int baserange_wn_converted = baserange_wn - WEEK_BASE;
    if (baserange_wn_converted < 0)
    {
        baserange_wn_converted = 0;
    }
    float baserange_time_in_sec = (baserange_wn_converted * SECONDS_IN_ONE_WEEK) + baserange_data->range_header.ms;

    printf("%s(%d)  baserange_cmp time: %f, acc_cn0_count: %d, top4_mean: %f, acc_mean: %f, sig_qual: %d\r\n", __func__, __LINE__, baserange_time_in_sec, acc_cn0_count, top4_mean, acc_mean, signal_quality);
}

void init_parsing_info(void)
{
    m_parsing_info.parsing_state = PARSING_INIT;
    m_parsing_info.msg_id = 0;
    m_parsing_info.total_frame_len = 0;
    m_parsing_info.had_parsed_len = 0;
    m_parsing_info.need_parse_len = 0;
    m_parsing_info.had_parsed_item_num = 0;
    m_parsing_info.left_size_in_last_element = 0;
    m_parsing_info.left_size_in_curr_element = 0;
    m_parsing_info.left_item_num_in_curr_element = 0;
    m_parsing_info.last_frame_info.last_loop_cnt = -1;
    m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_0;
    m_parsing_info.last_frame_info.msg_id_lo_byte = 0xFF;
    m_parsing_info.last_frame_info.big_len = 0;
    m_parsing_info.need_rd_new_data = false;
}

void init_statistics_info(void)
{
	m_rtk_statistics_info.baserange_found_frame_cnt = 0;
    m_rtk_statistics_info.baserange_verified_frame_cnt = 0;
	m_rtk_statistics_info.bestpos_found_frame_cnt = 0;
	m_rtk_statistics_info.bestpos_verified_frame_cnt = 0;
	m_rtk_statistics_info.bestsats_found_frame_cnt = 0;
	m_rtk_statistics_info.bestsats_verified_frame_cnt = 0;
	m_rtk_statistics_info.rangecmp_found_frame_cnt = 0;
	m_rtk_statistics_info.rangecmp_verified_frame_cnt = 0;
}

void init_other_stuff(void)
{
    memset(data_buf_for_crc, '\0', MAX_DATA_BUF_LEN);
}

void check_parsing_info(uint32_t line_number)
{
    printf("Line%d, parsing_state: %d, total_frame_len: %d, had_parsed_len: %d, need_parse_len: %d, \
        had_parsed_item_num: %d, left_size_in_last_element: %d, left_size_in_curr_element: %d, \
        left_item_num_in_curr_element: %d, last_loop_cnt: %d, last_byte_num: %d, last_bytes_big_len: %d\r\n", \
        line_number, \
        m_parsing_info.parsing_state, \
        m_parsing_info.total_frame_len, \
        m_parsing_info.had_parsed_len, \
        m_parsing_info.need_parse_len, \
        m_parsing_info.had_parsed_item_num, \
        m_parsing_info.left_size_in_last_element, \
        m_parsing_info.left_size_in_curr_element, \
        m_parsing_info.left_item_num_in_curr_element, \
        m_parsing_info.last_frame_info.last_loop_cnt, \
        m_parsing_info.last_frame_info.last_bytes_num, \
        m_parsing_info.last_frame_info.big_len
    );
}

int main(int argc, char *argv[])
{
    if (argc >= 2) {
        const char* file_path = argv[1];
        printf("file_path: %s\r\n", file_path);
        FILE* p_file = NULL;
        errno_t  open_ret = fopen_s(&p_file, file_path, "rb");
        printf("open_ret: %d\r\n", open_ret);
        if (0 != open_ret) {
            printf("Open fail, exit!\r\n");
            return -1;
        }


        fseek(p_file, 0, SEEK_END);
        int file_length = ftell(p_file);
        int total_DATASIZE_cnt = file_length / ELEMENTSIZE;

        printf("file_length: %d, total_DATASIZE_cnt: %d\r\n", file_length, total_DATASIZE_cnt);


        uint8_t buffer[RX_BUF_SIZE] = {'0'};
        fseek(p_file, 0, SEEK_SET);
        int loop_cnt = 0;
        int baserange_frame_cnt = 0;
        int baserange_verified_frame_cnt = 0;
        int bestpos_frame_cnt = 0;
        int bestpos_verified_frame_cnt = 0;
        int rangecmp_frame_cnt = 0;
        int rangecmp_verified_frame_cnt = 0;
        int bestsats_frame_cnt = 0;
        int bestsats_verified_frame_cnt = 0;

        uint16_t header_size = sizeof(GenericHeaderForBinaryMsg);
        printf("LINE%d, header_size: %d\r\n", __LINE__, header_size);
        bool check_header = false;


        uint16_t msg_id = 0;
        int baserange_curr_size = 0;
        int baserange_sub_item_size = 0;
        int baserange_observation_num_idx = 0;


        int bestsats_curr_size = 0;
        int bestsats_sub_item_size = 0;
        int bestsats_obs_num_idx = 0;

        bool need_parse = false;
        int need_parse_left_size = 0;
        bool need_search_in_this_frame = false;
        int start_index = 0;
        printf("size_of_buffer: %d\r\n", (int)sizeof(buffer));
        bool is_get_whole_frame = false;
        uint8_t combined_baserange_buf[ELEMENT_SIZE_BASERANGE] = {'\0'};
        memset(combined_baserange_buf, '\0', ELEMENT_SIZE_BASERANGE);

        init_parsing_info();
        init_statistics_info();
        init_other_stuff();
        while (!feof(p_file)) // to read file
        {
            printf("Last:    ");
            for (int k = 0; k < RX_BUF_SIZE; k++) {
                printf("%02x ", buffer[k]);
            }
            printf("\r\n");
            // function used to read the contents of file
            // 是否读新的数据取决于上一个ELEMENT数据是否已经处理完了
            if (m_parsing_info.need_rd_new_data) {
                size_t items_read_cnt = fread_s(buffer, BUFFERSIZE, ELEMENTSIZE, ELEMENTCOUNT, p_file);
                printf("items_read_cnt: %d\r\n", (int)items_read_cnt);
                printf("Current:    ");
                for (int k = 0; k < RX_BUF_SIZE; k++) {
                    printf("%02x ", buffer[k]);
                }
                printf("\r\n");
            }

            if (loop_cnt >= DEBUG_LINE_START and loop_cnt <= DEBUG_LINE_END) {
                printf("LINE%d, loop_cnt: %d, need_parse: %d\r\n", __LINE__, loop_cnt, need_parse);
            }

            switch (m_parsing_info.parsing_state)
            {
                case PARSING_INIT:
                {
                    break;
                }
                case FIND_HEADERSYNC_FROM_LAST_ELEMENT:
                {
                    switch (m_parsing_info.last_frame_info.last_bytes_num)
                    {
                        case LAST_BYTES_1:
                        {
                            if (GEN_SYNC1 == buffer[0]) {
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_1;
                            }
                            else {
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_0;
                            }
                            m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_CURR_ELEMENT;
                            break;
                        }
                        case LAST_BYTES_2:
                        {
                            if (GEN_SYNC1 == buffer[0] && GEN_SYNC2 == buffer[1]) {
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_2;
                            }
                            else if (GEN_SYNC1 == buffer[1]) {
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_1;
                            }
                            else {
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_0;
                            }
                            m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_CURR_ELEMENT;
                            break;
                        }
                        case LAST_BYTES_3:
                        {
                            if (GEN_SYNC1 == buffer[0] && GEN_SYNC2 == buffer[1] && GEN_SYNC3 == buffer[2]) {
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_3;
                            }
                            else if (GEN_SYNC1 == buffer[1] && GEN_SYNC2 == buffer[2]) {
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_2;
                            }
                            else if (GEN_SYNC1 == buffer[2]) {
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_1;
                            }
                            else {
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_0;
                            }
                            m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_CURR_ELEMENT;
                            break;
                        }
                        case LAST_BYTES_4:
                        {
                            if (GEN_SYNC1 == buffer[0] && GEN_SYNC2 == buffer[1] && GEN_SYNC3 == buffer[2] && GEN_HEAD_LEN == buffer[3]) {
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_4;
                                m_parsing_info.parsing_state = FOUND_HEADERSYNC;
                            }
                            else if (GEN_SYNC1 == buffer[1] && GEN_SYNC2 == buffer[2] && GEN_SYNC3 == buffer[3]) {
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_3;
                                m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_CURR_ELEMENT;
                            }
                            else if (GEN_SYNC1 == buffer[2] && GEN_SYNC2 == buffer[3]) {
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_2;
                                m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_CURR_ELEMENT;
                            }
                            else if (GEN_SYNC1 == buffer[3]) {
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_1;
                                m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_CURR_ELEMENT;
                            }
                            else {
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_0;
                                m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_CURR_ELEMENT;
                            }
                            break;
                        }
                        case LAST_BYTES_5:
                        {
                            if (GEN_SYNC1 == buffer[0] && GEN_SYNC2 == buffer[1] && GEN_SYNC3 == buffer[2] && GEN_HEAD_LEN == buffer[3]) {
                                m_parsing_info.last_frame_info.msg_id_lo_byte = buffer[4];
                                m_parsing_info.parsing_state = FOUND_HEADERSYNC;
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_5;
                            }
                            else if (GEN_SYNC1 == buffer[1] && GEN_SYNC2 == buffer[2] && GEN_SYNC3 == buffer[3] && GEN_HEAD_LEN == buffer[4]) {
                                m_parsing_info.parsing_state = FOUND_HEADERSYNC;
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_4;
                            }
                            else if (GEN_SYNC1 == buffer[2] && GEN_SYNC2 == buffer[3] && GEN_SYNC3 == buffer[4]) {
                                m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_CURR_ELEMENT;
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_3;
                            }
                            else if (GEN_SYNC1 == buffer[3] && GEN_SYNC2 == buffer[4]) {
                                m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_CURR_ELEMENT;
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_2;
                            }
                            else if (GEN_SYNC1 == buffer[4]) {
                                m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_CURR_ELEMENT;
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_1;
                            }
                            else {
                                m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_CURR_ELEMENT;
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_0;
                            }
                            break;
                        }
                        case LAST_BYTES_BIG:
                        {
                            /* 要找到帧头的第一件事就是找到0xAA这个字节 */
                            for (int i = 0; i < m_parsing_info.last_frame_info.big_len; i++) {
                                if (GEN_SYNC1 == buffer[i] && FIND_HEADERSYNC_FROM_LAST_ELEMENT == m_parsing_info.parsing_state) {
                                    if (i == m_parsing_info.last_frame_info.big_len-1)
                                    {
                                        m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_CURR_ELEMENT;
                                        m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_1;
                                        break;
                                    }
                                    else if (i == m_parsing_info.last_frame_info.big_len - 2)
                                    {
                                        if (GEN_SYNC2 == buffer[i+1]) {
                                            m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_CURR_ELEMENT;
                                            m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_2;
                                            break;
                                        }
                                        else {
                                            m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_LAST_ELEMENT;
                                        }
                                    }
                                    else if (i == m_parsing_info.last_frame_info.big_len - 3) {
                                        if (GEN_SYNC2 == buffer[i+1] && GEN_SYNC3 == buffer[i+2]) {
                                            m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_CURR_ELEMENT;
                                            m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_3;
                                            break;
                                        }
                                        else {
                                            m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_LAST_ELEMENT;
                                        }
                                    }
                                    else if (i == m_parsing_info.last_frame_info.big_len - 4) {
                                        if (GEN_SYNC2 == buffer[i+1] && GEN_SYNC3 == buffer[i+2] && GEN_HEAD_LEN == buffer[i+3]) {
                                            m_parsing_info.parsing_state = FOUND_HEADERSYNC;
                                            m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_4;
                                            break;
                                        }
                                        else {
                                            m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_LAST_ELEMENT;
                                        }
                                    }
                                    else if (i == m_parsing_info.last_frame_info.big_len - 5) {
                                        if (GEN_SYNC2 == buffer[i+1] && GEN_SYNC3 == buffer[i+2] && GEN_HEAD_LEN == buffer[i+3]) {
                                            m_parsing_info.parsing_state = FOUND_HEADERSYNC;
                                            m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_5;
                                            m_parsing_info.last_frame_info.msg_id_lo_byte = buffer[i+4];
                                            break;
                                        }
                                        else {
                                            m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_LAST_ELEMENT;
                                        }
                                    }
                                    else if (i <= m_parsing_info.last_frame_info.big_len - 5) {
                                        if (GEN_SYNC2 == buffer[i+1] && GEN_SYNC3 == buffer[i+2] && GEN_HEAD_LEN == buffer[i+3]) {
                                            m_parsing_info.parsing_state = FOUND_MSG_ID;
                                            m_parsing_info.msg_id = (uint16_t)(((uint16_t)buffer[i+5]) << 8) + buffer[i+4];
                                            m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_BIG;
                                            m_parsing_info.last_frame_info.big_len = m_parsing_info.last_frame_info.big_len - i;
                                            memcpy(&buffer[0], &buffer[i], m_parsing_info.last_frame_info.big_len);
                                            m_parsing_info.need_rd_new_data = false;
                                            break;
                                        }
                                    
                                    }

                                }
                            }
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                    break;
                }
                case FIND_HEADERSYNC_FROM_CURR_ELEMENT:
                {
                    break;
                }
                case FOUND_HEADERSYNC:        /* FOUND_HEADERSYNC只有2种情况，LAST_BYTES_4，LAST_BYTES_5 */
                {
                    switch (m_parsing_info.last_frame_info.last_bytes_num)
                    {
                        case LAST_BYTES_4:
                        {
                            break;
                        }
                        case LAST_BYTES_5:
                        {
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                    break;
                }
                case FOUND_MSG_ID:        /* FOUND_MSG_ID */
                {
                    switch (m_parsing_info.msg_id)
                    {
                        case MSG_ID_BASERANGE:
                        {
                            m_rtk_statistics_info.baserange_found_frame_cnt++;
                            if (m_parsing_info.last_frame_info.big_len >= (header_size + 4)) {
                                memcpy(&curr_baserange.range_header, &buffer[0], header_size);
                                memcpy(&curr_baserange.observation_num, &buffer[header_size], 4);
                                m_parsing_info.parsing_state = NEED_PARSE_BASERANGE_BODY;
                                m_parsing_info.total_frame_len = 36 + (ELEMENT_SIZE_BASERANGE * curr_baserange.observation_num);
                                m_parsing_info.left_size_in_last_element = m_parsing_info.last_frame_info.big_len - header_size - 4;
                                m_parsing_info.left_item_num_in_curr_element = m_parsing_info.left_size_in_last_element / ELEMENT_SIZE_BASERANGE;
                                m_parsing_info.left_size_in_curr_element = m_parsing_info.left_size_in_last_element % ELEMENT_SIZE_BASERANGE;
                                if (m_parsing_info.left_item_num_in_curr_element > 0) {
                                    memcpy(&curr_baserange.range_data[0], &buffer[header_size + 4], ELEMENT_SIZE_BASERANGE * m_parsing_info.left_item_num_in_curr_element);
                                }
                                if (m_parsing_info.left_size_in_curr_element > 0) {
                                    memcpy(&combined_baserange_buf[0], &buffer[header_size + 4 + (ELEMENT_SIZE_BASERANGE * m_parsing_info.left_item_num_in_curr_element)], \
                                        m_parsing_info.left_size_in_curr_element);
                                }
                            }
                            else if (m_parsing_info.last_frame_info.big_len >= header_size) {
                                memcpy(&curr_baserange.range_header, &buffer[0], header_size);
                                m_parsing_info.parsing_state = FIND_ITEM_NUM;
                                m_parsing_info.left_size_in_last_element = m_parsing_info.last_frame_info.big_len - header_size;
                                if (m_parsing_info.left_size_in_last_element > 0) {
                                    memcpy(&buffer[0], &buffer[header_size], m_parsing_info.left_size_in_last_element);
                                }
                            }
                            else {
                                m_parsing_info.parsing_state = FIND_HEADER;
                            
                            }
                            m_parsing_info.had_parsed_len += m_parsing_info.last_frame_info.big_len;
                            break;
                        }
                        case MSG_ID_RANGECMP:
                        {
                            m_rtk_statistics_info.rangecmp_found_frame_cnt++;
                            break;
                        }
                        case MSG_ID_BESTSATS:
                        {
                            m_rtk_statistics_info.bestsats_found_frame_cnt++;
                            break;
                        }
                        case MSG_ID_BESTPOS:
                        {
                            m_rtk_statistics_info.bestpos_found_frame_cnt++;
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                    break;
                }
                case FIND_HEADER:
                {
                    break;
                }
                case FOUND_HEADER:
                {
                    break;
                }
                case FIND_ITEM_NUM:
                {
                    break;
                }
                case FOUND_ITEM_NUM:
                {
                    break;
                }
                case NEED_PARSE_BESTPOS_BODY:
                {
                    break;
                }
                case NEED_PARSE_BASERANGE_BODY:
                {
                    check_parsing_info(__LINE__);
                    if (m_parsing_info.need_parse_len > ELEMENTSIZE)
                    {
                        memcpy(&combined_baserange_buf[m_parsing_info.left_size_in_last_element], &buffer[0], (ELEMENT_SIZE_BASERANGE - m_parsing_info.left_size_in_last_element));
                        memcpy(&curr_baserange.range_data[m_parsing_info.had_parsed_item_num], &combined_baserange_buf[0], ELEMENT_SIZE_BASERANGE);
                        m_parsing_info.had_parsed_item_num++;
                        memset(combined_baserange_buf, '\0', ELEMENT_SIZE_BASERANGE);
                        m_parsing_info.left_size_in_curr_element = ELEMENTSIZE - (ELEMENT_SIZE_BASERANGE - m_parsing_info.left_size_in_last_element);
                        m_parsing_info.left_item_num_in_curr_element = (uint16_t)(m_parsing_info.left_size_in_curr_element / ELEMENT_SIZE_BASERANGE);
                        memcpy(&curr_baserange.range_data[m_parsing_info.had_parsed_item_num], &buffer[ELEMENT_SIZE_BASERANGE - m_parsing_info.left_size_in_last_element], \
                            ELEMENT_SIZE_BASERANGE* m_parsing_info.left_item_num_in_curr_element);
                        m_parsing_info.left_size_in_last_element = (uint16_t)(m_parsing_info.left_size_in_curr_element % ELEMENT_SIZE_BASERANGE);
                        if (m_parsing_info.left_size_in_last_element > 0) {
                            memcpy(&combined_baserange_buf[0], &buffer[ELEMENTSIZE - m_parsing_info.left_size_in_last_element], m_parsing_info.left_size_in_last_element);
                        }
                        m_parsing_info.parsing_state = NEED_PARSE_BASERANGE_BODY;
                        m_parsing_info.had_parsed_len += ELEMENTSIZE;
                        m_parsing_info.had_parsed_item_num += m_parsing_info.left_item_num_in_curr_element;
                        m_parsing_info.need_parse_len -= ELEMENTSIZE;
                        m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_0;
                    }
                    else if (m_parsing_info.need_parse_len <= ELEMENTSIZE) {
                        memcpy(&combined_baserange_buf[m_parsing_info.left_size_in_last_element], &buffer[0], (ELEMENT_SIZE_BASERANGE - m_parsing_info.left_size_in_last_element));
                        memcpy(&curr_baserange.range_data[m_parsing_info.had_parsed_item_num], &combined_baserange_buf[0], ELEMENT_SIZE_BASERANGE);
                        m_parsing_info.had_parsed_item_num++;
                        memset(combined_baserange_buf, '\0', ELEMENT_SIZE_BASERANGE);
                        m_parsing_info.left_size_in_curr_element = m_parsing_info.need_parse_len - (ELEMENT_SIZE_BASERANGE - m_parsing_info.left_size_in_last_element);
                        m_parsing_info.left_item_num_in_curr_element = (uint16_t)(m_parsing_info.left_size_in_curr_element / ELEMENT_SIZE_BASERANGE);
                        memcpy(&curr_baserange.range_data[m_parsing_info.had_parsed_item_num], &buffer[ELEMENT_SIZE_BASERANGE - m_parsing_info.left_size_in_last_element], \
                            ELEMENT_SIZE_BASERANGE* m_parsing_info.left_item_num_in_curr_element);
                        // m_parsing_info.left_size_in_last_element应该为0才对。
                        m_parsing_info.left_size_in_last_element = (uint16_t)(m_parsing_info.left_size_in_curr_element % ELEMENT_SIZE_BASERANGE);
                        if (m_parsing_info.left_size_in_last_element > 0) {
                            printf("Line%d, Something must be wrong, left_size_in_last_element is desired to be 0, while has value: %d\r\n", \
                                __LINE__, m_parsing_info.left_size_in_last_element);
                        }
                        memcpy(&curr_baserange.crc, &buffer[m_parsing_info.need_parse_len - 4], 4);
                        m_parsing_info.parsing_state = PARSE_BASERANGE_DONE;
                        m_parsing_info.had_parsed_len += m_parsing_info.need_parse_len;
                        m_parsing_info.had_parsed_item_num += m_parsing_info.left_item_num_in_curr_element;
                        check_parsing_info(__LINE__);
                        m_parsing_info.need_parse_len = 0;
                        m_parsing_info.left_size_in_last_element = 0;
                        m_parsing_info.left_size_in_curr_element = 0;
                        m_parsing_info.left_item_num_in_curr_element = 0;
                        m_parsing_info.last_frame_info.last_loop_cnt = loop_cnt;
                        if (m_parsing_info.need_parse_len == ELEMENTSIZE) {
                            m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_0;
                            m_parsing_info.last_frame_info.big_len = 0;
                        }
                        else {
                            memcpy(&buffer[0], &buffer[m_parsing_info.need_parse_len], (ELEMENTSIZE - m_parsing_info.need_parse_len));
                            uint16_t unparsed_len = ELEMENTSIZE - m_parsing_info.need_parse_len;
                            if (unparsed_len > 5) {
                                m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_BIG;
                            }
                            else {
                                switch (unparsed_len) {
                                    case 1:
                                    {
                                        m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_1;
                                        break;
                                    }
                                    case 2:
                                    {
                                        m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_2;
                                        break;
                                    }
                                    case 3:
                                    {
                                        m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_3;
                                        break;
                                    }
                                    case 4:
                                    {
                                        m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_4;
                                        break;
                                    }
                                    case 5:
                                    {
                                        m_parsing_info.last_frame_info.last_bytes_num = LAST_BYTES_5;
                                        break;
                                    }
                                    default:
                                    {
                                        break;
                                    }
                                }
                            
                            }
                            m_parsing_info.last_frame_info.big_len = ELEMENTSIZE - m_parsing_info.need_parse_len;
                        }
                    }
                    check_parsing_info(__LINE__);
                    break;
                }
                case NEED_PARSE_BESTSATS_BODY:
                {
                    break;
                }
                case NEED_PARSE_RANGECMP_BODY:
                {
                    break;
                }
                case PARSE_BESTPOS_DONE:
                {
                    break;
                }
                case PARSE_BASERANGE_DONE:
                {
                    memcpy(&data_buf_for_crc[0], &curr_baserange.range_header, header_size);
                    memcpy(&data_buf_for_crc[header_size], &curr_baserange.observation_num, 4);
                    int real_data_size = m_parsing_info.total_frame_len - header_size - 4 - 4;
                    memcpy(&data_buf_for_crc[header_size+4], &curr_baserange.range_data[0], real_data_size);
                    memcpy(&data_buf_for_crc[header_size+4+real_data_size], &curr_baserange.crc[0], 4);
                    uint32_t base_received_crc = curr_baserange.crc[0] + \
                        (curr_baserange.crc[1] << 8) + \
                        (curr_baserange.crc[2] << 16) + \
                        (curr_baserange.crc[3] << 24);
                    bool check_crc_ret = checkCRC32(&data_buf_for_crc[0], m_parsing_info.total_frame_len, base_received_crc);
                    if (check_crc_ret == true)
                    {
                        printf("LINE%d, baserange CRC check pass!\r\n", __LINE__);
                        m_rtk_statistics_info.baserange_verified_frame_cnt++;
                        statisticsBaseStationSatellitesCN0(&curr_baserange);

                        is_get_whole_frame = false;
                        memset(&curr_baserange.range_header, '\0', header_size);
                        memset(&curr_baserange.observation_num, '\0', 4);
                        memset(&curr_baserange.range_data, '\0', baserange_curr_size - header_size - 4 - 4);
                        memset(&curr_baserange.crc, '\0', 4);
                        memset(data_buf_for_crc, '\0', MAX_DATA_BUF_LEN);
                    }
                    else {
                        printf("LINE%d, baserange CRC check fail, received crc0: 0x%02x, crc1: 0x%02x, crc2: 0x%02x, crc3: 0x%02x!\r\n", \
                            __LINE__, curr_baserange.crc[0], curr_baserange.crc[1], curr_baserange.crc[2], curr_baserange.crc[3]);
                    }
                    if (LAST_BYTES_0 == m_parsing_info.last_frame_info.last_bytes_num) {
                        m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_CURR_ELEMENT;
                        m_parsing_info.total_frame_len = 0;
                        m_parsing_info.had_parsed_len = 0;
                        m_parsing_info.need_parse_len = 0;
                        m_parsing_info.had_parsed_item_num = 0;
                        m_parsing_info.left_size_in_last_element = 0;
                        m_parsing_info.left_size_in_curr_element = 0;
                        m_parsing_info.left_item_num_in_curr_element = 0;
                        m_parsing_info.last_frame_info.last_loop_cnt = loop_cnt;
                    }
                    else if (LAST_BYTES_5 == m_parsing_info.last_frame_info.last_bytes_num) {
                        m_parsing_info.parsing_state = FIND_HEADERSYNC_FROM_LAST_ELEMENT;
                    }
                    break;
                }
                case PARSE_BESTSATS_DONE:
                {
                    break;
                }
                case PARSE_RANGECMP_DONE:
                {
                    break;
                }
                default:
                {
                    printf("Unknown parsing state: %d\r\n", m_parsing_info.parsing_state);
                    break;
                }
            }

            loop_cnt++;
            printf("loop_cnt: %d, baserange_frame_cnt: %d, baserange_verified_frame_cnt: %d, bestpos_frame_cnt: %d\r\n", \
                loop_cnt, baserange_frame_cnt, baserange_verified_frame_cnt, bestpos_frame_cnt);
        }
        return 0;
    } else {
        printf("Usage: ./rangefile_parser.exe rtk_raw_5mindata_via_bridge_1650_1655.log");
        exit(-1);
    }

	return 0;
}
