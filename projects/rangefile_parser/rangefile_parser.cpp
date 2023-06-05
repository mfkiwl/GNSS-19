// rangefile_parser.cpp: 定义应用程序的入口点。
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

#define BASERANGE_MAX_LEN  5756

#define DEBUG_LINE_START    715
#define DEBUG_LINE_END    722

uint8_t data_buf[BASERANGE_MAX_LEN] = {'\0'};
BESTPOSData m_bestpos_data;
RangeMeasurements curr_baserange;
UNCBESTSATSMsg curr_bestsats;

typedef enum {
    PARSING_INIT = 0,
    FIND_HEADER = 1,
    FOUND_HEADER = 2,
    NEED_PARSE_BESTPOS_BODY = 3,
    NEED_PARSE_BASERANGE_BODY = 4,
    NEED_PARSE_BESTSATS_BODY = 5,
    NEED_PARSE_RANGECMP_BODY = 6,
    PARSE_BESTPOS_DONE = 7,
    PARSE_BASERANGE_DONE = 8,
    PARSE_BESTSATS_DONE = 9,
    PARSE_RANGECMP_DONE = 10,
    PARSE_STATE_NUM = 11,
} PARSING_STATE;

typedef struct {
    PARSING_STATE parsing_state;
    uint32_t total_frame_len;        // total_frame_len = had_parsed_len + need_parse_len
    uint32_t had_parsed_len;
    uint32_t need_parse_len;
};


typedef enum {
    LAST_BYTES_0,
    LAST_BYTES_1,
    LAST_BYTES_2,
    LAST_BYTES_3,
    LAST_BYTES_4,
    LAST_BYTES_5,
    LAST_BYTES_BIG,
} LAST_BYTES_NUM;

typedef struct {
    int last_loop_cnt;
    LAST_BYTES_NUM last_bytes_num;
    uint8_t msg_id_lo_byte;
} LAST_FRAME_RECORD;

LAST_FRAME_RECORD m_last_frame_record;

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

    printf("%s(%d) satellites_of_l1: %d, satellites_of_l2: %d\r\n", __func__, __LINE__, satellites_of_l1, satellites_of_l2);
}


bool checkCarSatelliteInBestSatellite(UNCBESTSATSMsg* bestsats_data, uint32_t satellite_sys, uint32_t satellite_prn, uint32_t sig_type)
{
    uint32_t i;
    bool ret = false;
    for (i = 0; i < curr_bestsats.num_of_sats; i++)
    {
        uint32_t sat_prn = bestsats_data->sats_data[i].sat_prn;
        uint16_t bestsats_prn = ((uint8_t*)(&sat_prn))[2] | (((uint8_t*)(&sat_prn))[3] << 8));
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

    printf("%s(%d)  acc_cn0_count: %d, top4_mean: %f, acc_mean: %f, sig_qual: %d\r\n", __func__, __LINE__, acc_cn0_count, top4_mean, acc_mean, signal_quality);
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

        memset(data_buf, '\0', BASERANGE_MAX_LEN);

         fseek(p_file, 0, SEEK_END);
         int file_length = ftell(p_file);
         int total_DATASIZE_cnt = file_length / ELEMENTSIZE;

         printf("file_length: %d, total_DATASIZE_cnt: %d\r\n", file_length, total_DATASIZE_cnt);


         m_last_frame_record.last_loop_cnt = -1;
         m_last_frame_record.last_bytes_num = LAST_BYTES_0;

        uint8_t buffer[RX_BUF_SIZE] = {'0'};
        fseek(p_file, 0, SEEK_SET);
        int loop_cnt = 0;
        int baserange_frame_cnt = 0;
        int baserange_verified_frame_cnt = 0;
        int bestpos_frame_cnt = 0;
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
        uint8_t combined_buf[44] = {'\0'};
        memset(combined_buf, '\0', 44);
        while (!feof(p_file)) // to read file
        {
            printf("Last:    ");
            for (int k = 0; k < RX_BUF_SIZE; k++) {
                printf("%02x ", buffer[k]);
            }
            printf("\r\n");
            // function used to read the contents of file
            size_t items_read_cnt = fread_s(buffer, BUFFERSIZE, ELEMENTSIZE, ELEMENTCOUNT, p_file);
            printf("items_read_cnt: %d\r\n", (int)items_read_cnt);
            printf("Current:    ");
            for (int k = 0; k < RX_BUF_SIZE; k++) {
                printf("%02x ", buffer[k]);
            }
            printf("\r\n");

            if (loop_cnt >= DEBUG_LINE_START and loop_cnt <= DEBUG_LINE_END) {
                printf("LINE%d, loop_cnt: %d, need_parse: %d\r\n", __LINE__, loop_cnt, need_parse);
            }

            if (need_parse) {
                int local_left_size = 0;
                int local_left_obs_num = 0;
                if (need_parse_left_size > ELEMENTSIZE) {
                    printf("LINE%d, baserange_observation_num_idx: %d, baserange_sub_item_size: %d\r\n", __LINE__, baserange_observation_num_idx, baserange_sub_item_size);
                    memcpy(&combined_buf[baserange_sub_item_size], &buffer[0], (44 - baserange_sub_item_size));
                    memcpy(&curr_baserange.range_data[baserange_observation_num_idx], &combined_buf[0], 44);
                    memset(combined_buf, '\0', 44);
                    local_left_size = ELEMENTSIZE - (44 - baserange_sub_item_size);
                    local_left_obs_num = local_left_size / 44;
                    memcpy((&curr_baserange.range_data[baserange_observation_num_idx+1]), &buffer[44-baserange_sub_item_size], 44 * local_left_obs_num);
                    printf("\r\n");
                    printf("LINE%d, print range_data[%d]\r\n", __LINE__, baserange_observation_num_idx);
                    uint8_t tmp_debug_data[44] = {'\0'};
                    memset(tmp_debug_data, '\0', 44);
                    memcpy(tmp_debug_data, &curr_baserange.range_data[baserange_observation_num_idx], 44);
                    for (int y = 0; y < 44; y++) {
                        printf("%02x ", tmp_debug_data[y]);
                    }
                    printf("\r\n");
                    baserange_sub_item_size = local_left_size % 44;
                    memcpy((&combined_buf[0]), &buffer[ELEMENTSIZE-baserange_sub_item_size], baserange_sub_item_size);
                    baserange_observation_num_idx = baserange_observation_num_idx + 1 + local_left_obs_num;
                    printf("LINE%d, local_left_size: %d, local_left_obs_num: %d, baserange_sub_item_size: %d, baserange_observation_num_idx: %d\r\n", \
                        __LINE__, local_left_size, local_left_obs_num, baserange_sub_item_size, baserange_observation_num_idx);
                    need_parse = true;
                    is_get_whole_frame = false;
                    need_parse_left_size = need_parse_left_size - ELEMENTSIZE;
                    need_search_in_this_frame = false;
                    printf("LINE%d, need_parse_left_size: %d, baserange_observation_num_idx: %d, baserange_sub_item_size: %d\r\n", \
                        __LINE__, need_parse_left_size, baserange_observation_num_idx, baserange_sub_item_size);
                } else if (need_parse_left_size == ELEMENTSIZE) {
                    printf("LINE%d, baserange_observation_num_idx: %d, baserange_sub_item_size: %d\r\n", __LINE__, baserange_observation_num_idx, baserange_sub_item_size);
                    memcpy(&combined_buf[baserange_sub_item_size], &buffer[0], (44 - baserange_sub_item_size));
                    memcpy(&curr_baserange.range_data[baserange_observation_num_idx], &combined_buf[0], 44);
                    memset(combined_buf, '\0', 44);
                    local_left_size = ELEMENTSIZE - (44 - baserange_sub_item_size);
                    local_left_obs_num = local_left_size / 44;
                    baserange_sub_item_size = local_left_size % 44;
                    memcpy((&curr_baserange.range_data[baserange_observation_num_idx+1]), &buffer[44-baserange_sub_item_size], 44 * local_left_obs_num);
                    memcpy(&curr_baserange.crc, &buffer[ELEMENTSIZE-4], 4);
                    baserange_observation_num_idx = baserange_observation_num_idx + 1 + local_left_obs_num;
                    printf("LINE%d, local_left_size: %d, local_left_obs_num: %d, baserange_sub_item_size: %d, baserange_observation_num_idx: %d\r\n", \
                        __LINE__, local_left_size, local_left_obs_num, baserange_sub_item_size, baserange_observation_num_idx);
                    printf("LINE%d, Received crc0: %02x, crc1: %02x, crc2: %02x, crc3: %02x\r\n", \
                        __LINE__, curr_baserange.crc[0], curr_baserange.crc[1], curr_baserange.crc[2], curr_baserange.crc[3]);
                    printf("LINE%d, print range_data[%d]\r\n", __LINE__, baserange_observation_num_idx-1);
                    uint8_t tmp_debug_data[44] = {'\0'};
                    memset(tmp_debug_data, '\0', 44);
                    memcpy(tmp_debug_data, &curr_baserange.range_data[baserange_observation_num_idx-1], 44);
                    for (int y = 0; y < 44; y++) {
                        printf("%02x ", tmp_debug_data[y]);
                    }
                    printf("\r\n");
                    printf("LINE%d, baserange_observation_num_idx: %d, baserange_sub_item_size: %d\r\n", __LINE__, baserange_observation_num_idx, baserange_sub_item_size);
                    local_left_size = 0;
                    local_left_obs_num = 0;
                    need_parse = false;
                    need_parse_left_size = 0;
                    baserange_observation_num_idx = 0;
                    is_get_whole_frame = true;
                    baserange_sub_item_size = 0;
                    need_search_in_this_frame = false;
                } else if (need_parse_left_size < ELEMENTSIZE) {
                    printf("LINE%d, need_parse_left_size: %d, baserange_observation_num_idx: %d, baserange_sub_item_size: %d\r\n", __LINE__, need_parse_left_size, baserange_observation_num_idx, baserange_sub_item_size);
                    memcpy(&combined_buf[baserange_sub_item_size], &buffer[0], (44 - baserange_sub_item_size));
                    memcpy(&curr_baserange.range_data[baserange_observation_num_idx], &combined_buf[0], 44);
                    printf("LINE%d, print range_data[%d]\r\n", __LINE__, baserange_observation_num_idx);
                    uint8_t tmp_debug_data[44] = {'\0'};
                    memset(tmp_debug_data, '\0', 44);
                    memcpy(tmp_debug_data, &curr_baserange.range_data[baserange_observation_num_idx], 44);
                    for (int y = 0; y < 44; y++) {
                        printf("%02x ", tmp_debug_data[y]);
                    }
                    printf("\r\n");
                    memset(combined_buf, '\0', 44);
                    local_left_size = need_parse_left_size - (44 - baserange_sub_item_size);
                    local_left_obs_num = local_left_size / 44;
                    memcpy((&curr_baserange.range_data[baserange_observation_num_idx+1]), &buffer[44-baserange_sub_item_size], 44 * local_left_obs_num);
                    memcpy(&curr_baserange.crc, &buffer[need_parse_left_size-4], 4);
                    printf("LINE%d, local_left_size: %d, local_left_obs_num: %d, baserange_sub_item_size: %d, baserange_observation_num_idx: %d\r\n", \
                        __LINE__, local_left_size, local_left_obs_num, baserange_sub_item_size, baserange_observation_num_idx);
                    printf("LINE%d, Received crc0: %02x, crc1: %02x, crc2: %02x, crc3: %02x\r\n", \
                        __LINE__, curr_baserange.crc[0], curr_baserange.crc[1], curr_baserange.crc[2], curr_baserange.crc[3]);

                    local_left_size = 0;
                    local_left_obs_num = 0;
                    need_parse = false;
                    need_parse_left_size = 0;
                    baserange_observation_num_idx = 0;
                    baserange_sub_item_size = 0;
                    is_get_whole_frame = true;
                    need_search_in_this_frame = true;
                    start_index = need_parse_left_size;
                    m_last_frame_record.last_loop_cnt = loop_cnt;
                    m_last_frame_record.last_bytes_num = LAST_BYTES_BIG;
                }
            } else {
                if (loop_cnt == (m_last_frame_record.last_loop_cnt + 1)) {
                    switch (m_last_frame_record.last_bytes_num) {
                        case LAST_BYTES_0:
                        {
                            printf("LINE%d, Should never enter into this case!\r\n", __LINE__);
                            start_index = 0;
                            check_header = false;
                            break;
                        }
                        case LAST_BYTES_1:
                        {
                            if (GEN_SYNC2 == buffer[0] && GEN_SYNC3 == buffer[1] && GEN_HEAD_LEN == buffer[2]) {
                                printf("LINE%d, LAST_BYTES_1-combined success\r\n", __LINE__);
                                check_header = true;        // 检测到帧头
                                start_index = 3;
                                    // msg_id_LO: buffer[start_index]
                                    // msg_id_HI: buffer[start_index+1]
                            }
                            else {
                                check_header = false;
                                start_index = 0;
                                printf("LAST_BYTES_1-combined fail\r\n");
                            }
                            break;
                        }
                        case LAST_BYTES_2:
                        {
                            if (GEN_SYNC3 == buffer[0] && GEN_HEAD_LEN == buffer[1]) {
                                printf("LAST_BYTES_2-combined success\r\n");
                                check_header = true;        // 检测到帧头
                                start_index = 2;
                                    // msg_id_LO: buffer[start_index]
                                    // msg_id_HI: buffer[start_index+1]
                            }
                            else {
                                check_header = false;
                                start_index = 0;
                                printf("LAST_BYTES_2-combined fail\r\n");
                            }
                            break;
                        }
                        case LAST_BYTES_3:
                        {
                            if (GEN_HEAD_LEN == buffer[0]) {
                                printf("LAST_BYTES_3-combined success\r\n");
                                check_header = true;        // 检测到帧头
                                start_index = 1;
                                    // msg_id_LO: buffer[start_index]
                                    // msg_id_HI: buffer[start_index+1]
                            }
                            else {
                                check_header = false;        // 没有检测到帧头
                                start_index = 0;
                                printf("LAST_BYTES_3-combined fail\r\n");
                            }
                            break;
                        }
                        case LAST_BYTES_4:
                        {
                            check_header = true;        // 检测到帧头
                            start_index = 0;           // 表明当前帧第1个字节为msg_id低字节，第2个字节为msg_id高字节。
                                    // msg_id_LO: buffer[start_index]
                                    // msg_id_HI: buffer[start_index+1]
                            printf("LINE%d, LAST_BYTES_4\r\n", __LINE__);
                            break;
                        }
                        case LAST_BYTES_5:
                        {
                            check_header = true;        // 检测到帧头
                            start_index = -1;           // 表明msg_id 低字节在上一帧，高字节在当前帧。
                                    // msg_id_LO: m_last_frame_record.msg_id_lo_byte
                                    // msg_id_HI: buffer[start_index+1]
                            printf("LINE%d, LAST_BYTES_5\r\n", __LINE__);
                            break;
                        }
                        default:
                        {
                            start_index = 0;
                            check_header = false;
                            printf("Unknown case: last_bytes_num: %d\r\n", m_last_frame_record.last_bytes_num);
                            break;
                        }
                    }
                }
                else {
                    start_index = 0;
                    check_header = false;
                }
    
                if (loop_cnt >= DEBUG_LINE_START and loop_cnt <= DEBUG_LINE_END) {
                    printf("LINE%d, start_index: %d, check_header: %d, need_parse: %d\r\n", __LINE__, start_index, check_header, need_parse);
                    printf("LINE%d, last_loop_cnt: %d, last_bytes_num: %d\r\n", __LINE__, m_last_frame_record.last_loop_cnt, m_last_frame_record.last_bytes_num);
                }

                if (check_header) {        /* 已检测到帧头 */
                    uint8_t tmp_header[28] = {'\0'};
                    memset(tmp_header, '\0', 28);
                    tmp_header[0] = GEN_SYNC1;
                    tmp_header[1] = GEN_SYNC2;
                    tmp_header[2] = GEN_SYNC3;
                    tmp_header[3] = GEN_HEAD_LEN;
                    if (-1 == start_index) {    /* 说明是 LAST_BYTES_5 */
                        msg_id = (uint16_t)(((uint16_t)buffer[start_index+1]) << 8) + m_last_frame_record.msg_id_lo_byte;
                    } else if (start_index >=0 and start_index <= 3) {
                        msg_id = (uint16_t)(((uint16_t)buffer[start_index+1]) << 8) + buffer[start_index];
                    }
                    switch (msg_id) {
                        case MSG_ID_BASERANGE:
                        {
                            baserange_frame_cnt++;
                            if (-1 == start_index) {
                                tmp_header[4] = m_last_frame_record.msg_id_lo_byte;
                            }
                            memcpy(&tmp_header[4-start_index], &buffer[0], header_size-(4-start_index));
                            memcpy(&curr_baserange.range_header, &tmp_header[0], header_size);
                            memcpy(&curr_baserange.observation_num, &buffer[header_size-(4-start_index)], 4);
                            baserange_curr_size = 36 + (44 * curr_baserange.observation_num);
                            int curr_frame_valid_size = 1024;
                            int local_data_size = curr_frame_valid_size - (header_size - (4 - start_index)) - 4;
                            baserange_observation_num_idx = local_data_size / 44;
                            baserange_sub_item_size = local_data_size % 44;
                            memcpy(&curr_baserange.range_data[0], &buffer[header_size+start_index], baserange_observation_num_idx * 44);
                            memcpy(&combined_buf[0], &buffer[ELEMENTSIZE-baserange_sub_item_size], baserange_sub_item_size);
                            if (baserange_curr_size > curr_frame_valid_size) {
                                is_get_whole_frame = false;
                                need_parse = true;
                                need_parse_left_size = baserange_curr_size - curr_frame_valid_size - (4 - start_index);
                                need_search_in_this_frame = false;        // Need parse in next frame, no need to search in this frame
                            } else {
                                is_get_whole_frame = true;
                                need_parse = false;        // 当前帧已完成拷贝，无须解析下一帧
                                need_parse_left_size = 0;
                                need_search_in_this_frame = true;        // Need parse in next frame, no need to search in this frame
                                start_index = curr_frame_valid_size - baserange_curr_size;
                            }
                            printf("LINE%d, baserange_curr_size: %d, curr_frame_valid_size: %d, need_parse: %d, need_parse_left_size: %d\r\n", __LINE__, baserange_curr_size, curr_frame_valid_size, need_parse, need_parse_left_size);
                            printf("LINE%d, loop_cnt: %d, baserange_frame_cnt: %d, start_index: %d, br_obs_num: %d\r\n", __LINE__, loop_cnt, baserange_frame_cnt, start_index, curr_baserange.observation_num);
                            break;
                        }
                        case MSG_ID_BESTSATS:
                        {
                            bestsats_frame_cnt++;
                            if (-1 == start_index) {
                                tmp_header[4] = m_last_frame_record.msg_id_lo_byte;
                            }
                            memcpy(&tmp_header[4-start_index], &buffer[0], header_size-(4-start_index));
                            memcpy(&curr_bestsats.hdr_msg, &tmp_header[0], header_size);
                            memcpy(&curr_bestsats.num_of_sats, &buffer[header_size-(4-start_index)], 4);
                            bestsats_curr_size = 36 + (BESTSATS_ITEM_SIZE * curr_bestsats.num_of_sats);
                            int curr_frame_valid_size = 1024;
                            int local_data_size = curr_frame_valid_size - (header_size - (4 - start_index)) - 4;
                            bestsats_obs_num_idx = local_data_size / BESTSATS_ITEM_SIZE;
                            bestsats_sub_item_size = local_data_size % BESTSATS_ITEM_SIZE;
                            memcpy(&curr_bestsats.sats_data[0], &buffer[header_size + start_index], bestsats_obs_num_idx* BESTSATS_ITEM_SIZE);
                            memcpy(&combined_buf[0], &buffer[ELEMENTSIZE-bestsats_sub_item_size], bestsats_sub_item_size);
                            if (bestsats_curr_size > curr_frame_valid_size) {
                                is_get_whole_frame = false;
                                need_parse = true;
                                need_parse_left_size = bestsats_curr_size - curr_frame_valid_size - (4 - start_index);
                                need_search_in_this_frame = false;        // Need parse in next frame, no need to search in this frame
                            } else {
                                is_get_whole_frame = true;
                                need_parse = false;        // 当前帧已完成拷贝，无须解析下一帧
                                need_parse_left_size = 0;
                                need_search_in_this_frame = true;        // Need parse in next frame, no need to search in this frame
                                start_index = curr_frame_valid_size - bestsats_curr_size;
                            }
                            printf("LINE%d, bestsats_curr_size: %d, curr_frame_valid_size: %d, need_parse: %d, need_parse_left_size: %d\r\n", __LINE__, bestsats_curr_size, curr_frame_valid_size, need_parse, need_parse_left_size);
                            printf("LINE%d, loop_cnt: %d, bestsats_frame_cnt: %d, start_index: %d, bestsats_obs_num: %d\r\n", __LINE__, loop_cnt, bestsats_frame_cnt, start_index, curr_bestsats.num_of_sats);
                            break;
                        }
                        case MSG_ID_BESTPOS:
                        {
                            bestpos_frame_cnt++;
                            memcpy(&tmp_header[4-start_index], &buffer[0], header_size-(4-start_index));
                            memcpy(&m_bestpos_data.data[0], &tmp_header[0], header_size);
                            memcpy(&m_bestpos_data.data[header_size], &buffer[header_size-(4-start_index)], FIX_LENGTH_BESTPOS_MATCHEDPOS-header_size);
                            need_search_in_this_frame = true;
                            start_index = FIX_LENGTH_BESTPOS_MATCHEDPOS - start_index;
                            break;
                        }
                        default: {
                            break;
                        }
                    }
                    if (!need_parse) {
                        start_index = 0;
                    }
                    if (loop_cnt >= DEBUG_LINE_START and loop_cnt <= DEBUG_LINE_END) {
                        printf("LINE%d, need_parse: %d\r\n", __LINE__, need_parse);
                    }
                } else {        /* 没有检测到帧头 */
                    start_index = 0;
                    need_parse = false;
                    need_search_in_this_frame = true;
                }
                m_last_frame_record.last_loop_cnt = -1;
                m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                m_last_frame_record.msg_id_lo_byte = 0xff;
            }

            if (loop_cnt >= DEBUG_LINE_START and loop_cnt <= DEBUG_LINE_END) {
                printf("LINE%d, is_get_whole_frame: %d\r\n", __LINE__, is_get_whole_frame);
            }
            if (is_get_whole_frame) {
                memcpy(&data_buf[0], &curr_baserange.range_header, header_size);
                memcpy(&data_buf[header_size], &curr_baserange.observation_num, 4);
                int real_data_size = baserange_curr_size - header_size - 4 - 4;
                memcpy(&data_buf[header_size+4], &curr_baserange.range_data[0], real_data_size);
                memcpy(&data_buf[header_size+4+real_data_size], &curr_baserange.crc[0], 4);
                uint32_t base_received_crc = curr_baserange.crc[0] + \
                    (curr_baserange.crc[1] << 8) + \
                    (curr_baserange.crc[2] << 16) + \
                    (curr_baserange.crc[3] << 24);
                // bool check_crc_ret = checkCRC32((uint8_t *) & curr_baserange.range_header, baserange_curr_size, base_received_crc);
                for (int n = 0; n < baserange_curr_size; n++) {
                    printf("%02x ", data_buf[n]);
                }
                printf("\r\n");
                bool check_crc_ret = checkCRC32(&data_buf[0], baserange_curr_size, base_received_crc);
                if (check_crc_ret == true)
                {
                    printf("LINE%d, CRC check pass!\r\n", __LINE__);
                    baserange_verified_frame_cnt++;
                    is_get_whole_frame = false;
                    memset(&curr_baserange.range_header, '\0', header_size);
                    memset(&curr_baserange.observation_num, '\0', 4);
                    memset(&curr_baserange.range_data, '\0', baserange_curr_size - header_size - 4 - 4);
                    memset(&curr_baserange.crc, '\0', 4);
                    memset(data_buf, '\0', BASERANGE_MAX_LEN);
                }
                else {
                    printf("LINE%d, CRC check fail!\r\n", __LINE__);
                }
            }

            if (loop_cnt >= DEBUG_LINE_START && loop_cnt <= DEBUG_LINE_END) {
                printf("LINE%d, need_search_in_this_frame: %d, start_index: %d\r\n", __LINE__, need_search_in_this_frame, start_index);
            }
            if (need_search_in_this_frame) {
                check_header = false;
                for (int j = start_index; (j+5) < DATASIZE; j++) {
                    if (GEN_SYNC1 == buffer[j] && GEN_SYNC2 == buffer[j + 1] && GEN_SYNC3 == buffer[j + 2] && GEN_HEAD_LEN == buffer[j + 3]) {
                        check_header = true;
                        msg_id = (uint16_t)(((uint16_t)buffer[j + 5]) << 8) + buffer[j + 4];
                    }

                    if (check_header) {
                        switch (msg_id) {
                            case MSG_ID_BASERANGE:
                            {
                                baserange_frame_cnt++;
                                printf("LINE%d, buffer[%d]: %02x\r\n", __LINE__, (j+header_size-start_index), buffer[j+header_size-start_index]);
                                printf("LINE%d, buffer[%d]: %02x\r\n", __LINE__, (j+header_size-start_index+1), buffer[j+header_size-start_index+1]);
                                printf("LINE%d, buffer[%d]: %02x\r\n", __LINE__, (j+header_size-start_index+2), buffer[j+header_size-start_index+2]);
                                printf("LINE%d, buffer[%d]: %02x\r\n", __LINE__, (j+header_size-start_index+3), buffer[j+header_size-start_index+3]);
                                memcpy(&curr_baserange.range_header, &buffer[j], header_size);
                                memcpy(&curr_baserange.observation_num, &buffer[j+header_size-start_index], 4);
                                baserange_curr_size = 36 + (44 * curr_baserange.observation_num);
                                int curr_frame_valid_size = 1024 - j;
                                int local_data_size = curr_frame_valid_size - header_size - 4;
                                baserange_observation_num_idx = local_data_size / 44;
                                baserange_sub_item_size = local_data_size % 44;

                                if (baserange_curr_size > curr_frame_valid_size) {
                                    memcpy(&curr_baserange.range_data[0], &buffer[j+header_size-start_index+4], curr_frame_valid_size - header_size - 4);
                                    memcpy(&combined_buf[0], &buffer[ELEMENTSIZE-baserange_sub_item_size], baserange_sub_item_size);
                                    is_get_whole_frame = false;
                                    need_parse = true;
                                    need_parse_left_size = baserange_curr_size - curr_frame_valid_size;
                                    need_search_in_this_frame = false;
                                } else {
                                    memcpy(&curr_baserange.range_data[0], &buffer[j+header_size-start_index+4], (baserange_curr_size-header_size-4));
                                    is_get_whole_frame = true;
                                    need_parse = false;
                                    need_parse_left_size = 0;
                                    need_search_in_this_frame = true;
                                    start_index = curr_frame_valid_size - baserange_curr_size;
                                }
                                printf("LINE%d, baserange_curr_size: %d, curr_frame_valid_size: %d, need_parse: %d, need_parse_left_size: %d\r\n", __LINE__, baserange_curr_size, curr_frame_valid_size, need_parse, need_parse_left_size);
                                printf("LINE%d, start_index: %d, br_obs_num: %d\r\n", __LINE__, start_index, curr_baserange.observation_num);
                                printf("LINE%d, loop_cnt: %d, j: %d, start_index: %d, baserange_frame_cnt: %d\r\n", __LINE__, loop_cnt, j, start_index, baserange_frame_cnt);
                                break;
                            }
                            default: {
                                break;
                            }
                        }
                    }
                    if (need_parse) {
                        break;
                    }
                }
                if (false == check_header) {
                    start_index = 0;
                    need_search_in_this_frame = false;
                    m_last_frame_record.last_loop_cnt = -1;
                    m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                }
    
                if (!need_parse) {
                    for (int m = (DATASIZE - 5); m < DATASIZE; m++) {
                        if (GEN_SYNC1 == buffer[m]) {
                            printf("--buffer[%d]: 0x%02x\r\n", m, buffer[m]);
                            switch (m) {
                                case (DATASIZE - 5):
                                {
                                    if (GEN_SYNC2 == buffer[m+1] && GEN_SYNC3 == buffer[m+2] && GEN_HEAD_LEN == buffer[m+3]) {
                                        printf("--AA--buffer[%d]: 0x%02x\r\n", m, buffer[m]);
                                        m_last_frame_record.last_loop_cnt = loop_cnt;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_5;
                                        m_last_frame_record.msg_id_lo_byte = buffer[m+4];
                                    }
                                    else {
                                        m_last_frame_record.last_loop_cnt = -1;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                                        m_last_frame_record.msg_id_lo_byte = 0xff;
                                    }
                                    break;
                                }
                                case (DATASIZE - 4):
                                {
                                    if (GEN_SYNC2 == buffer[m+1] && GEN_SYNC3 == buffer[m+2] && GEN_HEAD_LEN == buffer[m+3]) {
                                        printf("--BB--buffer[%d]: 0x%02x\r\n", m, buffer[m]);
                                        m_last_frame_record.last_loop_cnt = loop_cnt;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_4;
                                        m_last_frame_record.msg_id_lo_byte = 0xff;
                                    }
                                    else {
                                        m_last_frame_record.last_loop_cnt = -1;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                                        m_last_frame_record.msg_id_lo_byte = 0xff;
                                    }
                                    break;
                                }
                                case (DATASIZE - 3):
                                {
                                    if (GEN_SYNC2 == buffer[m+1] && GEN_SYNC3 == buffer[m+2]) {
                                        printf("--CC--buffer[%d]: 0x%02x\r\n", m, buffer[m]);
                                        m_last_frame_record.last_loop_cnt = loop_cnt;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_3;
                                        m_last_frame_record.msg_id_lo_byte = 0xff;
                                    }
                                    else {
                                        m_last_frame_record.last_loop_cnt = -1;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                                        m_last_frame_record.msg_id_lo_byte = 0xff;
                                    }
                                    break;
                                }
                                case (DATASIZE - 2):
                                {
                                    if (GEN_SYNC2 == buffer[m + 1]) {
                                        printf("--DD--buffer[%d]: 0x%02x\r\n", m, buffer[m]);
                                        m_last_frame_record.last_loop_cnt = loop_cnt;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_2;
                                        m_last_frame_record.msg_id_lo_byte = 0xff;
                                    }
                                    else {
                                        m_last_frame_record.last_loop_cnt = -1;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                                        m_last_frame_record.msg_id_lo_byte = 0xff;
                                    }
                                    break;
                                }
                                case (DATASIZE - 1):
                                {
                                    printf("--EE--buffer[%d]: 0x%02x\r\n", m, buffer[m]);
                                    m_last_frame_record.last_loop_cnt = loop_cnt;
                                    m_last_frame_record.last_bytes_num = LAST_BYTES_1;
                                    m_last_frame_record.msg_id_lo_byte = 0xff;
                                    break;
                                }
                                default: {
                                    m_last_frame_record.last_loop_cnt = -1;
                                    m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                                    m_last_frame_record.msg_id_lo_byte = 0xff;
                                    printf("LINE%d, Unexpected value: %d\r\n", __LINE__, m);
                                    break;
                                }
                            }
                            if (loop_cnt == m_last_frame_record.last_loop_cnt) {
                                break;
                            }
                        }
                        else {
                            m_last_frame_record.last_loop_cnt = -1;
                            m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                            m_last_frame_record.msg_id_lo_byte = 0xff;
                        }
                    }
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
