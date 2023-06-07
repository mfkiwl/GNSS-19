#ifndef UNICORE_H
#define UNICORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#define RTK_UM960_MODEL	"UM960"
#define RTK_UM980_MODEL	"UM980"

#define RTK_LOGE(fmt, ...)   elog_e("RTK", "%s(%d)" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define RTK_LOGW(fmt, ...)   elog_w("RTK", "%s(%d)" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define RTK_LOGI(fmt, ...)   elog_i("RTK", "%s(%d)" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define RTK_LOGD(fmt, ...)   elog_d("RTK", "%s(%d)" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define RTK_LOGV(fmt, ...)   elog_v("RTK", "%s(%d)" fmt, __func__, __LINE__, ##__VA_ARGS__)

#define SUPPORT_AGRIC    0

#define MAX_BASERANGE_LEN    180
#define ELEMENT_SIZE_BASERANGE    44
#define BASERANGE_MAX_LEN  36 + (44 * MAX_BASERANGE_LEN)    // 7956 Bytes
#define MAX_DATA_BUF_LEN    BASERANGE_MAX_LEN

#define MAX_BESTSATS_LEN      80
#define MAX_RANGECMP_LEN      180

#define FIX_LENGTH_BESTPOS_MATCHEDPOS    104

typedef enum {
    CHIP_UNKNOWN = -1,
    CHIP_UM980 = 0,
    CHIP_UM960 = 1,
    CHIP_OTHER = 2,
} RTK_CHIP_TYPE;

#pragma pack(1)
typedef struct {
	uint32_t type;
	char fw_ver[33];
	char auth[129];
	char psn[66];
	char efuse_id[33];
	char comp_time[43];
} unc_devinfo_s;
#pragma pack()

typedef struct {
	bool valid;
	char model[64];
	char fw_ver[33];
} rtk_devinfo_s;

#define GEN_SYNC1        0xAA
#define GEN_SYNC2        0x44
#define GEN_SYNC3        0x12
#define GEN_HEAD_LEN     0x1C


typedef enum {
    MSG_ID_BESTPOS = 0x2A,                 // 42
    MSG_ID_MATCHEDPOS = 0x60,              // 96
    MSG_ID_PSRVEL = 0x64,                  // 100
    MSG_ID_TIME = 0x65,                    // 101
    MSG_ID_RANGECMP = 0x8C,                // 140
    MSG_ID_RTKPOS = 0x8D,                  // 141
    MSG_ID_REFSTATION = 0xAF,              // 175
    MSG_ID_RTKDATA = 0xD7,                 // 215
    MSG_ID_BASERANGE = 0x011B,             // 283
    MSG_ID_RTKSTATUS = 0x01FD,             // 509
    MSG_ID_JAMSTATUS = 0x01FF,             // 511
    MSG_ID_BESTSATS = 0x04AA,              // 1194
} UNC_MSG_IDS;

typedef struct {
    uint8_t sync1;
    uint8_t sync2;
    uint8_t sync3;
    uint8_t head_length;    // Fix length as 0x1C=28 Bytes
    uint16_t message_id;
    uint8_t message_type;    // 0x00 - Binary message, 0x01 - ASCII, 0x10 - Simplified ASCII
    uint8_t reserved1;
    uint16_t message_length;    // in Bytes, exclude Header and CRC
    uint16_t reserved2;
    uint8_t idle_time;
    uint8_t time_status;    // 20 - UNKNOWN receiver time, 160 - receiver had calculated FINE(accurate) time
    uint16_t week;    // GPS week number
    uint32_t ms;    // time which in ms within GPS week
    uint32_t reserved3;
    uint16_t bds_time_offset_to_gps;
    uint16_t reserved4;
} GenericHeaderForBinaryMsg;

typedef struct {
    uint8_t sync1;
    uint8_t sync2;
    uint8_t sync3;        // 0xB5
    uint8_t cpu_idle;    // CPUIDle 0 - 100
    uint16_t message_id;
    uint16_t message_length;    // in Bytes, exclude Header and CRC, for rtkstatusb, it is 56.
    uint8_t time_ref;        // GPST or BDST
    uint8_t time_status;    // 20 - UNKNOWN receiver time, 160 - receiver had calculated FINE(accurate) time
    uint16_t week;    // GPS week number
    uint32_t ms;    // time which in ms within GPS week
    uint32_t reserved1;
    uint8_t version;
    uint8_t leap_sec;
    uint16_t delay_ms;
} GenericHeaderForBinaryMsg2;

typedef struct {
	  uint8_t sync1;
	  uint8_t sync2;
	  uint8_t sync3;
    uint8_t cpu_idle;
	  uint16_t message_id;
	  uint16_t message_length;
	  uint8_t time_ref;
	  uint8_t time_status;
	  uint16_t wn;
	  uint32_t ms;
	  uint32_t res;
	  uint8_t version;
	  uint8_t leap_sec;
	  uint16_t delay_ms;

	  char gnss[4];
	  uint8_t length;
	  uint8_t year;
		uint8_t month;
		uint8_t day;
		uint8_t hour;
		uint8_t minute;
	  uint8_t second;
	  uint8_t rtk_status;
		uint8_t heading_status;
	  uint8_t num_gps_sta;
	  uint8_t num_bds_sta;
		uint8_t num_glo_sta;
		float baseline_N;
		float baseline_E;
		float baseline_U;
	  float baseline_N_std;
		float baseline_E_std;
		float baseline_U_std;
		float heading;
		float pitch;
		float roll;
		float speed;
		float vn;
		float ve;
		float vu;
	  float vn_std;
		float ve_std;
		float vu_std;
		double lat;
		double lon;
		double alt;
		double ecef_x;
		double ecef_y;
		double ecef_z;
		float lat_std;
		float lon_std;
		float alt_std;
		float ecef_x_std;
		float ecef_y_std;
		float ecef_z_std;
		double base_lat;
		double base_lon;
		double base_alt;
		double sec_lat;
		double sec_lon;
		double sec_alt;
		uint32_t week_second;
		float diffage;
		float speed_heading;
		float undulation;
		float remain_float_3;
		float remain_float_4;
		uint8_t num_gal_sta;
		uint8_t remain_char_2;
		uint8_t remain_char_3;
		uint8_t remain_char_4;
		uint32_t crc;
} UnicoreAGRICMsg;

typedef union {
	 uint8_t data[256];
	 UnicoreAGRICMsg msg;
} AGRICData;

#pragma pack (4)
typedef struct {
    GenericHeaderForBinaryMsg header_msg;

    uint32_t clock_status;
    double clock_offset;
    double clock_std_offset;
    double utc_offset_based_on_gps_time;
    uint32_t utc_year;
    uint8_t utc_month;
    uint8_t utc_day;
    uint8_t utc_hour;
    uint8_t utc_minute;
    uint32_t utc_ms;
    uint32_t utc_status;    // 0: Invalid 1: Valid 2: Warning
    uint32_t crc;
} UnicoreTIMEMsg;    // TIME Msg occupies 76 bytes

typedef struct {
    GenericHeaderForBinaryMsg header_msg;

    uint32_t sol_status;
    uint32_t pos_type;
    double lat;
    double lon;
    double hgt;
    float undulation;
    uint32_t datum_id_number;
    float lat_std;
    float lon_std;
    float hgt_std;
    uint32_t reference_stn_id;
    float diff_age;
    float sol_age;
    uint8_t satellites_num_on_search;
    uint8_t satellites_num_in_use;
    uint8_t reserved1;
    uint8_t reserved2;
    uint8_t reserved3;
    uint8_t extern_sol_stat;
    uint8_t galileo_sig_mask;
    uint8_t gps_glo_bds_sig_mask;
    uint32_t crc;
} UnicoreBESTPOSMsg;    // BESTPOS Msg occupies 104 bytes

typedef struct {
    GenericHeaderForBinaryMsg header_msg;

    uint32_t sol_status;
    uint32_t pos_type;
    double lat;
    double lon;
    double hgt;
    float undulation;
    uint32_t datum_id_number;
    float lat_std;
    float lon_std;
    float hgt_std;
    uint32_t reference_stn_id;
    float diff_age;
    float sol_age;
    uint8_t satellites_num_on_search;
    uint8_t satellites_num_in_use;        // filed 16
    uint8_t reserved1;
    uint8_t satellites_num_l2_dualfreq;        // filed 18
    uint8_t reserved3;
    uint8_t extern_sol_stat;
    uint8_t galileo_sig_mask;
    uint8_t gps_glo_bds_sig_mask;
    uint32_t crc;
} UnicoreMATCHEDPOSMsg;    // MATCHEDPOS Msg occupies 104 bytes

typedef struct {
    GenericHeaderForBinaryMsg header_msg;

    uint32_t sol_status;
    uint32_t vel_type;
    float latency;
    float diff_age;
    double hor_spd;
    double trk_gnd;
    double vert_spd;
    uint16_t hor_spd_error;
    uint16_t vert_spd_error;
    uint32_t crc;
} UnicorePSRVELMsg;    // PSRVEL Msg occupies 76 bytes

typedef struct {
    GenericHeaderForBinaryMsg2 header_msg;

    uint32_t gps_source;
    uint32_t reserved1;
    uint32_t bds_source1;
    uint32_t bds_source2;
    uint32_t reserved2;
    uint32_t glo_source;
    uint32_t reserved3;
    uint32_t gal_source1;
    uint32_t gal_source2;
    uint32_t qzss_source;
    uint32_t reserved4;
    uint32_t pos_type;
    uint32_t calc_status;
    uint8_t ion_detected;
    uint8_t reserved5;
    uint8_t reserved6;
    uint8_t reserved7;
    uint32_t crc;
} UnicoreRTKSTATUSMsg;    // RTKSTATUS Msg occupies 84 bytes

typedef struct {
    GenericHeaderForBinaryMsg header_msg;
    uint32_t status;
    double x;
    double y;
    double z;
    uint32_t health;
    uint32_t stn_type;
    char stn_id[8];
    uint32_t crc;
} UnicoreREFSTATIONMsg;  // REFSTATION Msg occupies 52 bytes

typedef struct {
    uint32_t channel_tracking_status;
    uint32_t doppfreq_pseudorange;
    uint32_t pseudorange;
    uint32_t adr_carrier_phase;
    uint8_t psr_adr_std;
    uint8_t prn_per_slot;
    uint32_t lock_time_cn0_xxx;
    uint16_t reserved1;
} UnicoreOneRangecmpMsg;    // One Rangecmp Msg occupies 24 bytes

typedef struct {
    GenericHeaderForBinaryMsg header_msg;
    uint32_t total_obs_num;
    UnicoreOneRangecmpMsg items[MAX_RANGECMP_LEN];
    uint32_t crc;
} UnicoreRangecmpMsg;    // RangecmpMsg may occupies, 28 + 4 + (24 * 130) + 4 = 3156

#pragma pack()

typedef union {
    uint8_t data[104];
    UnicoreBESTPOSMsg msg;
} BESTPOSData;

typedef union {
    uint8_t data[104];
    UnicoreMATCHEDPOSMsg msg;
} MATCHEDPOSData;

typedef union {
	 uint8_t data[76];
	 UnicorePSRVELMsg msg;
} PSRVELData;

typedef union {
	 uint8_t data[76];
	 UnicoreTIMEMsg msg;
} TIMEData;

typedef union {
    uint8_t data[76];
    UnicoreREFSTATIONMsg msg;
} REFSTATIONData;


#pragma pack (2)
typedef struct {
    uint32_t track_state:5;
    uint32_t sv_chan_num:5;
    uint32_t phase_lock_flag:1;
    uint32_t parity_known_flag:1;
    uint32_t code_locked_flag:1;
    uint32_t correlator_type:3;
    uint32_t satellite_sys:3;
    uint32_t reserved:1;
    uint32_t grouping:1;
    uint32_t signal_type:5;
    uint32_t forward_err_corection:1;
    uint32_t primary_L1_chan:1;
    uint32_t carrier_phase_meas:1;
    uint32_t reserved2:1;
    uint32_t prn_lock_flag:1;
    uint32_t carrier_assign:1;
} ChannelStatus;    // One ChannelStatus occupies 4 bytes

typedef struct {
    int64_t doppler_freq:28;
    uint64_t pseudorange:36;
    int32_t accumulated_doppler:32;
    uint16_t pseudorange_sd:4;
    uint16_t accumulated_doppler_sd:4;
    uint16_t satellite_prn:8;
    uint32_t locktime:21;
    uint32_t carrier_to_noise:5;
    uint32_t glo_freq_no:6;
    uint16_t reserved:16;
} CompressedRangeRecord;    // One CompressedRangeRecord occupies 20 bytes

typedef struct {
    int64_t doppler_freq:28;
    uint64_t pseudorange:36;
    int32_t accumulated_doppler:32;
    uint16_t pseudorange_sd:4;
    uint16_t accumulated_doppler_sd:4;
    uint8_t sat_prn;
    float locktime;
    uint8_t carrier_to_noise;
} CompressedRangeRecordReadable;    // One compress_range_record occupies 20 bytes

typedef struct {
    ChannelStatus channel_status;
    CompressedRangeRecord range_record;
} CompressedRangeData;
#pragma pack ()

#pragma pack (4)
typedef struct {
    uint16_t sat_prn;
    uint16_t glo_freq;
    double pseudorange;
    float pseudorange_sd;
	double accumulated_doppler;
    float accumulated_doppler_sd;
    float doppler;
    float carrier_to_noise;
    float locktime;
    ChannelStatus channel_status;
} RangeRecord;    // One RangeRecord occupies 44 bytes
#pragma pack ()

#pragma pack (4)
typedef struct {
    GenericHeaderForBinaryMsg range_header;                // Message header
    uint32_t observation_num;                              // Number of ranges observations in the following message
    RangeRecord range_data[MAX_BASERANGE_LEN];             // Range data for each available channel
    uint8_t crc[4];
} RangeMeasurements;
#pragma pack ()

typedef struct {
    uint32_t sat_sys;
    uint32_t sat_prn;
    uint32_t sat_status;
    uint32_t sat_signal_mask;
} UncBESTSATSOneRecord;

typedef struct {
    GenericHeaderForBinaryMsg hdr_msg;
    uint32_t num_of_sats;
    UncBESTSATSOneRecord sats_data[MAX_BESTSATS_LEN];
    uint8_t crc[4];
} UNCBESTSATSMsg;


#pragma pack (4)
typedef struct {
    GenericHeaderForBinaryMsg cmp_range_header;            // Message header
    uint32_t observation_num;                              // Number of ranges observations in the following message
    CompressedRangeData range_data[MAX_RANGECMP_LEN];   // Range data for each available channel
    uint8_t crc[4];
} CompressedRangeMesaurements;
#pragma pack ()








typedef struct {
    uint32_t satellite_sys;
    uint16_t satellite_prn;
    uint32_t satellite_signal_type;
    float satellite_lock_time;
}last_time_rangebase_t;

typedef struct {
    uint32_t satellite_sys;
    uint16_t satellite_prn;
    uint32_t satellite_signal_type;
    float satellite_lock_time;
}last_time_rangecmp_t;

typedef enum {
    GENERAL_SatSys_GPS = 0,
    GENERAL_SatSys_GLONASS = 1,
    GENERAL_SatSys_SBAS = 2,
    GENERAL_SatSys_GAL = 5,
    GENERAL_SatSys_BDS = 6,
    GENERAL_SatSys_QZSS = 7,
    GENERAL_SatSys_NAVIC = 9,
    GENERAL_SatSys_OTHER = 10,
} GENERAL_SatSys;

// BEGIN ChannelStatus
typedef enum {
    SatSys_GPS = 0,
    SatSys_GLONASS = 1,
    SatSys_SBAS = 2,
    SatSys_GAL = 3,
    SatSys_BDS = 4,
    SatSys_QZSS = 5,
    SatSys_OTHER = 6,
} CHNL_STAT_SatSys;

typedef enum {
    GPS_L1CA = 0,
    GPS_L2P_W = 5,
    GPS_L5_DATA = 6,
    GPS_L2P = 9,
    GPS_L5Q = 14,
    GPS_L2C = 17,
} SAT_GPS_SIGNAL_TYPE;

typedef enum {
    GLO_L1C = 0,
    GLO_L2C = 5,
} SAT_GLO_SIGNAL_TYPE;

typedef enum {
    QZSS_L1C = 0,
    QZSS_L5_DATA = 6,
    QZSS_L2P = 9,
    QZSS_L5Q = 14,
    QZSS_L2C = 17,
} SAT_QZSS_SIGNAL_TYPE;

typedef enum {
    BDS_B1I = 0,
    BDS_B1Q = 4,
    BDS_B2Q = 5,
    BDS_B3Q = 6,
    BDS_B1CQ = 8,
    BDS_B2AQ = 12,
    BDS_B2B = 13,
    BDS_B2I = 17,
    BDS_B3I = 21,
    BDS_B1C_DATA = 23,
    BDS_B2A_DATA = 28,
} SAT_BDS_SIGNAL_TYPE;

typedef enum {
    GAL_E1B = 1,
    GAL_E1C = 2,
    GAL_E5AQ = 12,
    GAL_E5BQ = 17,
} SAT_GAL_SIGNAL_TYPE;

typedef enum {
    SBAS_L1CA = 0,
    SBAS_L5_DATA = 6,
} SAT_SBAS_SIGNAL_TYPE;

typedef enum {
    DIFF_AGE_STATUS_GOOD,
    DIFF_AGE_STATUS_NORMAL,
    DIFF_AGE_STATUS_BAD,
    DIFF_AGE_STATUS_NONE,
}diff_age_status_e;

typedef enum {
    SIGNAL_QUALITY_GOOD,
    SIGNAL_QUALITY_NORMAL,
    SIGNAL_QUALITY_BAD,
    SIGNAL_QUALITY_NONE,
}signal_quality_e;

typedef enum {
    POS_PRECISION_GOOD,
    POS_PRECISION_NORMAL,
    POS_PRECISION_BAD,
    POS_PRECISION_NONE,
}pos_precision_e;

typedef enum {
    POS_STATE_GOOD,
    POS_STATE_NORMAL,
    POS_STATE_BAD,
    POS_STATE_NONE,
}pos_state_e;


void unicoreRtkDataCallback(uint8_t* data, uint16_t rx_len,uint32_t time_tick);

int unicoreCheckHeader(uint8_t*data,uint16_t rx_len);
int unicoreUnpack(uint8_t *data,uint16_t data_length,uint16_t *pack_length);
void unc_rtk_parsing(uint8_t *data,uint32_t rx_len);
void bestpos_rtk_sync_rtc_time(TIMEData *bestpos_utc);
int unc_rtk_firmware_version(char *version, int max_len);
int unc_rtk_model(char *model, int max_len);
RTK_CHIP_TYPE gnss_init(void);

#ifdef __cplusplus
}
#endif

#endif // UNICORE_H

