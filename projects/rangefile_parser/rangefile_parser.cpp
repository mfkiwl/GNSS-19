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

uint8_t data_buf[BASERANGE_MAX_LEN] = {'\0'};
RangeMeasurements curr_baserange;

typedef enum {
    LAST_BYTES_0,
    LAST_BYTES_1,
    LAST_BYTES_2,
    LAST_BYTES_3,
} LAST_BYTES_NUM;

typedef struct {
    int last_loop_cnt;
    LAST_BYTES_NUM last_bytes_num;
} LAST_FRAME_RECORD;

LAST_FRAME_RECORD m_last_frame_record;

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
        int bestpos_frame_cnt = 0;

        uint16_t header_size = sizeof(GenericHeaderForBinaryMsg);
        printf("LINE%d, header_size: %d\r\n", __LINE__, header_size);

        uint16_t msg_id = 0;
        int baserange_curr_size = 0;
        bool need_parse = false;
        int need_parse_left_size = 0;
        int observation_num_idx = 0;
        int br_sub_item_size = 0;
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

            if (need_parse) {
                int local_left_size = 0;
                int local_left_obs_num = 0;
                if (need_parse_left_size > ELEMENTSIZE) {
                    printf("LINE%d, observation_num_idx: %d, br_sub_item_size: %d\r\n", __LINE__, observation_num_idx, br_sub_item_size);
                    memcpy(&combined_buf[br_sub_item_size], &buffer[0], (44 - br_sub_item_size));
                    memcpy(&curr_baserange.range_data[observation_num_idx], &combined_buf[0], 44);
                    memset(combined_buf, '\0', 44);
                    local_left_size = ELEMENTSIZE - (44 - br_sub_item_size);
                    local_left_obs_num = local_left_size / 44;
                    memcpy((&curr_baserange.range_data[observation_num_idx+1]), &buffer[44-br_sub_item_size], 44 * local_left_obs_num);
                    printf("\r\n");
                    printf("LINE%d, print range_data[%d]\r\n", __LINE__, observation_num_idx);
                    uint8_t tmp_debug_data[44] = {'\0'};
                    memset(tmp_debug_data, '\0', 44);
                    memcpy(tmp_debug_data, &curr_baserange.range_data[observation_num_idx], 44);
                    for (int y = 0; y < 44; y++) {
                        printf("%02x ", tmp_debug_data[y]);
                    }
                    printf("\r\n");
                    br_sub_item_size = local_left_size % 44;
                    memcpy((&combined_buf[0]), &buffer[ELEMENTSIZE-br_sub_item_size], br_sub_item_size);
                    observation_num_idx = observation_num_idx + 1 + local_left_obs_num;
                    printf("LINE%d, local_left_size: %d, local_left_obs_num: %d, br_sub_item_size: %d, observation_num_idx: %d\r\n", \
                        __LINE__, local_left_size, local_left_obs_num, br_sub_item_size, observation_num_idx);
                    need_parse = true;
                    is_get_whole_frame = false;
                    need_parse_left_size = need_parse_left_size - ELEMENTSIZE;
                    need_search_in_this_frame = false;
                    printf("LINE%d, need_parse_left_size: %d, observation_num_idx: %d, br_sub_item_size: %d\r\n", \
                        __LINE__, need_parse_left_size, observation_num_idx, br_sub_item_size);
                } else if (need_parse_left_size == ELEMENTSIZE) {
                    printf("LINE%d, observation_num_idx: %d, br_sub_item_size: %d\r\n", __LINE__, observation_num_idx, br_sub_item_size);
                    memcpy(&combined_buf[br_sub_item_size], &buffer[0], (44 - br_sub_item_size));
                    memcpy(&curr_baserange.range_data[observation_num_idx], &combined_buf[0], 44);
                    memset(combined_buf, '\0', 44);
                    local_left_size = ELEMENTSIZE - (44 - br_sub_item_size);
                    local_left_obs_num = local_left_size / 44;
                    br_sub_item_size = local_left_size % 44;
                    memcpy((&curr_baserange.range_data[observation_num_idx+1]), &buffer[44-br_sub_item_size], 44 * local_left_obs_num);
                    memcpy(&curr_baserange.crc, &buffer[ELEMENTSIZE-4], 4);
                    observation_num_idx = observation_num_idx + 1 + local_left_obs_num;
                    printf("LINE%d, local_left_size: %d, local_left_obs_num: %d, br_sub_item_size: %d, observation_num_idx: %d\r\n", \
                        __LINE__, local_left_size, local_left_obs_num, br_sub_item_size, observation_num_idx);
                    printf("LINE%d, Received crc0: %02x, crc1: %02x, crc2: %02x, crc3: %02x\r\n", \
                        __LINE__, curr_baserange.crc[0], curr_baserange.crc[1], curr_baserange.crc[2], curr_baserange.crc[3]);
                    printf("LINE%d, print range_data[%d]\r\n", __LINE__, observation_num_idx-1);
                    uint8_t tmp_debug_data[44] = {'\0'};
                    memset(tmp_debug_data, '\0', 44);
                    memcpy(tmp_debug_data, &curr_baserange.range_data[observation_num_idx-1], 44);
                    for (int y = 0; y < 44; y++) {
                        printf("%02x ", tmp_debug_data[y]);
                    }
                    printf("\r\n");
                    printf("LINE%d, observation_num_idx: %d, br_sub_item_size: %d\r\n", __LINE__, observation_num_idx, br_sub_item_size);
                    local_left_size = 0;
                    local_left_obs_num = 0;
                    need_parse = false;
                    need_parse_left_size = 0;
                    observation_num_idx = 0;
                    is_get_whole_frame = true;
                    br_sub_item_size = 0;
                    need_search_in_this_frame = false;
                } else if (need_parse_left_size < ELEMENTSIZE) {
                    printf("LINE%d, need_parse_left_size: %d, observation_num_idx: %d, br_sub_item_size: %d\r\n", __LINE__, need_parse_left_size, observation_num_idx, br_sub_item_size);
                    memcpy(&combined_buf[br_sub_item_size], &buffer[0], (44 - br_sub_item_size));
                    memcpy(&curr_baserange.range_data[observation_num_idx], &combined_buf[0], 44);
                    printf("LINE%d, print range_data[%d]\r\n", __LINE__, observation_num_idx);
                    uint8_t tmp_debug_data[44] = {'\0'};
                    memset(tmp_debug_data, '\0', 44);
                    memcpy(tmp_debug_data, &curr_baserange.range_data[observation_num_idx], 44);
                    for (int y = 0; y < 44; y++) {
                        printf("%02x ", tmp_debug_data[y]);
                    }
                    printf("\r\n");
                    memset(combined_buf, '\0', 44);
                    local_left_size = need_parse_left_size - (44 - br_sub_item_size);
                    local_left_obs_num = local_left_size / 44;
                    memcpy((&curr_baserange.range_data[observation_num_idx+1]), &buffer[44-br_sub_item_size], 44 * local_left_obs_num);
                    memcpy(&curr_baserange.crc, &buffer[need_parse_left_size-4], 4);
                    printf("LINE%d, local_left_size: %d, local_left_obs_num: %d, br_sub_item_size: %d, observation_num_idx: %d\r\n", \
                        __LINE__, local_left_size, local_left_obs_num, br_sub_item_size, observation_num_idx);
                    printf("LINE%d, Received crc0: %02x, crc1: %02x, crc2: %02x, crc3: %02x\r\n", \
                        __LINE__, curr_baserange.crc[0], curr_baserange.crc[1], curr_baserange.crc[2], curr_baserange.crc[3]);

                    local_left_size = 0;
                    local_left_obs_num = 0;
                    need_parse = false;
                    need_parse_left_size = 0;
                    observation_num_idx = 0;
                    br_sub_item_size = 0;
                    is_get_whole_frame = true;
                    need_search_in_this_frame = true;
                    start_index = need_parse_left_size;
                }
            } else {
                if (loop_cnt == m_last_frame_record.last_loop_cnt) {
                    switch (m_last_frame_record.last_bytes_num) {
                        case LAST_BYTES_0:
                        {
                            printf("Should never enter into this case!\r\n");
                            start_index = 0;
                            break;
                        }
                        case LAST_BYTES_1:
                        {
                            if (GEN_SYNC2 == buffer[0] && GEN_SYNC3 == buffer[1] && GEN_HEAD_LEN == buffer[2]) {
                                printf("LAST_BYTES_1-combined success\r\n");
                                start_index = 3;
                            }
                            else {
                                start_index = 0;
                                printf("LAST_BYTES_1-combined fail\r\n");
                            }
                            break;
                        }
                        case LAST_BYTES_2:
                        {
                            if (GEN_SYNC3 == buffer[0] && GEN_HEAD_LEN == buffer[1]) {
                                printf("LAST_BYTES_2-combined success\r\n");
                                start_index = 2;
                            }
                            else {
                                start_index = 0;
                                printf("LAST_BYTES_2-combined fail\r\n");
                            }
                            break;
                        }
                        case LAST_BYTES_3:
                        {
                            if (GEN_HEAD_LEN == buffer[0]) {
                                printf("LAST_BYTES_3-combined success\r\n");
                                start_index = 1;
                            }
                            else {
                                start_index = 0;
                                printf("LAST_BYTES_3-combined fail\r\n");
                            }
                            break;
                        }
                        default:
                        {
                            start_index = 0;
                            printf("Unknown case: last_bytes_num: %d\r\n", m_last_frame_record.last_bytes_num);
                            break;
                        }
                    }
                }
                else {
                    start_index = 0;
                }
    
    
                for (int j = start_index; (j+3) < DATASIZE; j++) {
                    if (0 == start_index) {
                        if (GEN_SYNC1 == buffer[j] && GEN_SYNC2 == buffer[j + 1] && GEN_SYNC3 == buffer[j + 2] && GEN_HEAD_LEN == buffer[j + 3]) {
                            msg_id = (uint16_t)(((uint16_t)buffer[j + 5]) << 8) + buffer[j + 4];
                        }
                    }
                    else {
                        msg_id = (uint16_t)(((uint16_t)buffer[j + 1]) << 8) + buffer[j];
                    }
    
                    switch (msg_id) {
                        case MSG_ID_BASERANGE:
                        {
                            baserange_frame_cnt++;
                            printf("LINE%d, buffer[%d]: %02x\r\n", __LINE__, (j+header_size-start_index), buffer[j+header_size-start_index]);
                            printf("LINE%d, buffer[%d]: %02x\r\n", __LINE__, (j+header_size-start_index+1), buffer[j+header_size-start_index+1]);
                            printf("LINE%d, buffer[%d]: %02x\r\n", __LINE__, (j+header_size-start_index+2), buffer[j+header_size-start_index+2]);
                            printf("LINE%d, buffer[%d]: %02x\r\n", __LINE__, (j+header_size-start_index+3), buffer[j+header_size-start_index+3]);
                            memcpy(&curr_baserange.range_header, &buffer[j-start_index], header_size);
                            memcpy(&curr_baserange.observation_num, &buffer[j-start_index+header_size], 4);
                            baserange_curr_size = 36 + (44 * curr_baserange.observation_num);
                            int curr_frame_valid_size = 1024 - (j-start_index);
                            int local_data_size = curr_frame_valid_size - header_size - 4;
                            observation_num_idx = local_data_size / 44;
                            br_sub_item_size = local_data_size % 44;
                            memcpy(&curr_baserange.range_data[0], &buffer[j+header_size-start_index+4], observation_num_idx * 44);
                            memcpy(&combined_buf[0], &buffer[ELEMENTSIZE-br_sub_item_size], br_sub_item_size);

                            if (baserange_curr_size > curr_frame_valid_size) {
                                is_get_whole_frame = false;
                                need_parse = true;
                                need_search_in_this_frame = false;        // Need parse in next frame, no need to search in this frame
                                need_parse_left_size = baserange_curr_size - curr_frame_valid_size;
                            }
                            printf("LINE%d, baserange_curr_size: %d, curr_frame_valid_size: %d, need_parse: %d, need_parse_left_size: %d\r\n", __LINE__, baserange_curr_size, curr_frame_valid_size, need_parse, need_parse_left_size);
                            printf("LINE%d, start_index: %d, br_obs_num: %d\r\n", __LINE__, start_index, curr_baserange.observation_num);
                            printf("LINE%d, loop_cnt: %d, j: %d, start_index: %d, baserange_frame_cnt: %d\r\n", __LINE__, loop_cnt, j, start_index, baserange_frame_cnt);
                            break;
                        }
                        case MSG_ID_BESTPOS:
                        {
                            bestpos_frame_cnt++;
                            printf("LINE%d, bestpos_frame_cnt: %d\r\n", __LINE__, bestpos_frame_cnt);
                            break;
                        }
                        default: {
                            break;
                        }
                    }
                    if (need_parse) {
                        break;
                    }
                }
    
                if (!need_parse) {
                    for (int m = (DATASIZE - 3); m < DATASIZE; m++) {
                        if (GEN_SYNC1 == buffer[m]) {
                            printf("--buffer[%d]: 0x%02x\r\n", m, buffer[m]);
                            switch (m) {
                                case (DATASIZE - 3):
                                {
                                    if (GEN_SYNC2 == buffer[m+1] && GEN_SYNC3 == buffer[m+2]) {
                                        printf("--AA--buffer[%d]: 0x%02x\r\n", m, buffer[m]);
                                        m_last_frame_record.last_loop_cnt = loop_cnt;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_3;
                                    }
                                    else {
                                        m_last_frame_record.last_loop_cnt = -1;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                                    }
                                    break;
                                }
                                case (DATASIZE - 2):
                                {
                                    if (GEN_SYNC2 == buffer[m + 1]) {
                                        printf("--BB--buffer[%d]: 0x%02x\r\n", m, buffer[m]);
                                        m_last_frame_record.last_loop_cnt = loop_cnt;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_2;
                                    }
                                    else {
                                        m_last_frame_record.last_loop_cnt = -1;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                                    }
                                    break;
                                }
                                case (DATASIZE - 1):
                                {
                                    printf("--CC--buffer[%d]: 0x%02x\r\n", m, buffer[m]);
                                    m_last_frame_record.last_loop_cnt = loop_cnt;
                                    m_last_frame_record.last_bytes_num = LAST_BYTES_1;
                                    break;
                                }
                                default: {
                                    m_last_frame_record.last_loop_cnt = -1;
                                    m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                                    printf("Unexpected value: %d\r\n", m);
                                    break;
                                }
                            }
                        }
                        else {
                            m_last_frame_record.last_loop_cnt = -1;
                            m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                        }
                    }
                }
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
                }
                else {
                    printf("LINE%d, CRC check fail!\r\n", __LINE__);
                }

            }


            if (need_search_in_this_frame) {
                for (int j = start_index; (j+3) < DATASIZE; j++) {
                    if (0 == start_index) {
                        if (GEN_SYNC1 == buffer[j] && GEN_SYNC2 == buffer[j + 1] && GEN_SYNC3 == buffer[j + 2] && GEN_HEAD_LEN == buffer[j + 3]) {
                            msg_id = (uint16_t)(((uint16_t)buffer[j + 5]) << 8) + buffer[j + 4];
                        }
                    }
                    else {
                        msg_id = (uint16_t)(((uint16_t)buffer[j + 1]) << 8) + buffer[j];
                    }
    
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
                            observation_num_idx = local_data_size / 44;
                            br_sub_item_size = local_data_size % 44;
                            memcpy(&curr_baserange.range_data[0], &buffer[j+header_size-start_index+4], curr_frame_valid_size - header_size - 4);
                            // memcpy(&data_buf[0], &buffer[j], curr_frame_valid_size);

                            
                            if (baserange_curr_size > curr_frame_valid_size) {
                                need_parse = true;
                                need_parse_left_size = baserange_curr_size - curr_frame_valid_size;
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
                    if (need_parse) {
                        break;
                    }
                }
    
                if (!need_parse) {
                    for (int m = (DATASIZE - 3); m < DATASIZE; m++) {
                        if (GEN_SYNC1 == buffer[m]) {
                            printf("--buffer[%d]: 0x%02x\r\n", m, buffer[m]);
                            switch (m) {
                                case (DATASIZE - 3):
                                {
                                    if (GEN_SYNC2 == buffer[m+1] && GEN_SYNC3 == buffer[m+2]) {
                                        printf("--AA--buffer[%d]: 0x%02x\r\n", m, buffer[m]);
                                        m_last_frame_record.last_loop_cnt = loop_cnt;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_3;
                                    }
                                    else {
                                        m_last_frame_record.last_loop_cnt = -1;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                                    }
                                    break;
                                }
                                case (DATASIZE - 2):
                                {
                                    if (GEN_SYNC2 == buffer[m + 1]) {
                                        printf("--BB--buffer[%d]: 0x%02x\r\n", m, buffer[m]);
                                        m_last_frame_record.last_loop_cnt = loop_cnt;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_2;
                                    }
                                    else {
                                        m_last_frame_record.last_loop_cnt = -1;
                                        m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                                    }
                                    break;
                                }
                                case (DATASIZE - 1):
                                {
                                    printf("--CC--buffer[%d]: 0x%02x\r\n", m, buffer[m]);
                                    m_last_frame_record.last_loop_cnt = loop_cnt;
                                    m_last_frame_record.last_bytes_num = LAST_BYTES_1;
                                    break;
                                }
                                default: {
                                    m_last_frame_record.last_loop_cnt = -1;
                                    m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                                    printf("Unexpected value: %d\r\n", m);
                                    break;
                                }
                            }
                        }
                        else {
                            m_last_frame_record.last_loop_cnt = -1;
                            m_last_frame_record.last_bytes_num = LAST_BYTES_0;
                        }
                    }
                }
            }

            loop_cnt++;
            printf("loop_cnt: %d, baserange_frame_cnt: %d, bestpos_frame_cnt: %d\r\n", loop_cnt, baserange_frame_cnt, bestpos_frame_cnt);
        }
        return 0;
    } else {
        printf("Usage: ./rangefile_parser.exe rtk_raw_5mindata_via_bridge_1650_1655.log");
        exit(-1);
    }

	return 0;
}
