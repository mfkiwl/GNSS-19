// rangefile_parser.cpp: 定义应用程序的入口点。
//

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include "include/rangefile_parser.h"
#include "include/unicore.h"

/**
    Baserange size: 36 + 44 * M，when M = 130，5756 Bytes



**/


using namespace std;

#define RX_BUF_SIZE  1024U

#define BUFFERSIZE 1024U
#define DATASIZE 1024U
#define ELEMENTCOUNT 1
#define ELEMENTSIZE (DATASIZE/ELEMENTCOUNT)

#define BASERANGE_MAX_LEN  5756

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

        uint16_t header_size = sizeof(GenericHeaderForBinaryMsg);
        printf("LINE%d, header_size: %d\r\n", __LINE__, header_size);


        while (!feof(p_file)) // to read file
        {
            for (int k = 0; k < RX_BUF_SIZE; k++) {
                printf("%02x ", buffer[k]);
            }
            printf("\r\n");
            // function used to read the contents of file
            printf("size_of_buffer: %d\r\n", (int)sizeof(buffer));
            size_t items_read_cnt = fread_s(buffer, BUFFERSIZE, ELEMENTSIZE, ELEMENTCOUNT, p_file);
            printf("items_cnt: %d\r\n", (int)items_read_cnt);
            for (int k = 0; k < RX_BUF_SIZE; k++) {
                printf("%d - 0x%02x ", k, buffer[k]);
            }
            printf("\r\n");

            int start_index = 0;
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

            uint16_t msg_id = 0;

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
                        uint32_t br_obs_num = 0;
                        memcpy(&br_obs_num, &buffer[header_size-start_index], 4);
                        printf("LINE%d, start_index: %d, br_obs_num: %d\r\n", __LINE__, start_index, br_obs_num);
                        printf("LINE%d, loop_cnt: %d, j: %d, start_index: %d, baserange_frame_cnt: %d\r\n", __LINE__, loop_cnt, j, start_index, baserange_frame_cnt);
                        break;
                    }
                    default: {
                        break;
                    }
                }
            }

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

            loop_cnt++;
            printf("loop_cnt: %d, baserange_frame_cnt: %d\r\n", loop_cnt, baserange_frame_cnt);
        }
        return 0;
    } else {
        printf("Usage: ./rangefile_parser.exe rtk_raw_5mindata_via_bridge_1650_1655.log");
        exit(-1);
    }

	return 0;
}
