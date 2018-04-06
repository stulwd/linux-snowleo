#ifndef __FT5X02_CONFIG_H__ 
#define __FT5X02_CONFIG_H__ 
/*FT5X02 config*/

//∞≤≈‡A9003Q 20131028
#define FT5402_START_RX			0
#define FT5402_ADC_TARGET			8500
#define FT5402_KX			150
#define FT5402_KY			152
#define FT5402_RESOLUTION_X			600
#define FT5402_RESOLUTION_Y			1024
#define FT5402_LEMDA_X			0
#define FT5402_LEMDA_Y			0
#define FT5402_PWMODE_CTRL			1
#define FT5402_POINTS_SUPPORTED			5
#define FT5402_DRAW_LINE_TH			150
#define FT5402_FACE_DETECT_MODE			0
#define FT5402_FACE_DETECT_STATISTICS_TX_NUM			3
#define FT5402_FACE_DETECT_PRE_VALUE			20
#define FT5402_FACE_DETECT_NUM			10
#define FT5402_THGROUP			20
#define FT5402_THPEAK			60
#define FT5402_BIGAREA_PEAK_VALUE_MIN			75
#define FT5402_BIGAREA_DIFF_VALUE_OVER_NUM			45
#define FT5402_MIN_DELTA_X			2
#define FT5402_MIN_DELTA_Y			2
#define FT5402_MIN_DELTA_STEP			2
#define FT5402_ESD_DIFF_VAL			20
#define FT5402_ESD_NEGTIVE			-50
#define FT5402_ESD_FILTER_FRAME			10
#define FT5402_MAX_TOUCH_VALUE			600
#define FT5402_CUSTOMER_ID			121
#define FT5402_IO_LEVEL_SELECT			0
#define FT5402_DIRECTION			1
#define FT5402_POINTID_DELAY_COUNT			3
#define FT5402_LIFTUP_FILTER_MACRO			1
#define FT5402_POINTS_STABLE_MACRO			1
#define FT5402_ESD_NOISE_MACRO			0
#define FT5402_RV_G_PERIOD_ACTIVE			16
#define FT5402_DIFFDATA_HANDLE			0
#define FT5402_MIN_WATER_VAL			-50
#define FT5402_MAX_NOISE_VAL			10
#define FT5402_WATER_HANDLE_START_RX			0
#define FT5402_WATER_HANDLE_START_TX			0
#define FT5402_HOST_NUMBER_SUPPORTED			0
#define FT5402_RV_G_RAISE_THGROUP			30
#define FT5402_RV_G_CHARGER_STATE			0
#define FT5402_RV_G_FILTERID_START			2
#define FT5402_FRAME_FILTER_EN			0
#define FT5402_FRAME_FILTER_SUB_MAX_TH			2
#define FT5402_FRAME_FILTER_ADD_MAX_TH			2
#define FT5402_FRAME_FILTER_SKIP_START_FRAME			6
#define FT5402_FRAME_FILTER_BAND_EN			1
#define FT5402_FRAME_FILTER_BAND_WIDTH			128
#define FT5402_OTP_PARAM_ID			0


unsigned char g_ft5402_tx_num = 27;
unsigned char g_ft5402_rx_num = 16;
unsigned char g_ft5402_gain = 20;//15;
unsigned char g_ft5402_voltage = 3;
unsigned char g_ft5402_scanselect = 12;//10;
unsigned char g_ft5402_tx_order[] = {26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0};
unsigned char g_ft5402_tx_offset = 0;
unsigned char g_ft5402_tx_cap[] = {55,52,50,50,50,50,50,50,50,50,50,48,48,46,46,46,45,45,45,45,45,45,45,45,45,45,48};
unsigned char g_ft5402_rx_order[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
unsigned char g_ft5402_rx_offset[] = {69,69,101,85,86,85,101,85};
unsigned char g_ft5402_rx_cap[] = {100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100};


#endif