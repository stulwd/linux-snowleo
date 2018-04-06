/* 
 * drivers/input/touchscreen/ft5x0x_ts.c
 *
 * FocalTech ft5x0x TouchScreen driver. 
 *
 * Copyright (c) 2010  Focal tech Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * VERSION      	DATE			AUTHOR        Note
 *    1.0	       2010-01-05		WenFS     only support mulititouch	Wenfs 2010-10-01
 *    2.0          2011-09-05       Duxx      Add touch key, and project setting update, auto CLB command
 *    2.0-FS       2013-12-17       LeeSheen  Add Android 4.0, and in FS210 1024x600.
 *    2.0-FS       2014-03-30       LeeSheen  Auto UPDATE
 *     
 *
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include "ft5x02_ts.h"
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/interrupt.h>
#include <linux/delay.h>
//#include <asm/jzsoc.h>
#include <linux/slab.h>
#include <linux/io.h>
//#include <mach/regs-irq.h>  

#include <linux/gpio.h>
//#include <mach/smu.h>
//#include <mach/emev_board.h>
#include <linux/irq.h>
#include "ft5x_ts.h"

static int ft5x06_irq;

int lcd_flag=0;
static int chip_type = 0;//FT芯片类型

#ifdef UPDATE_BY_I_FILE
int ft5x02_Init_IC_Param(struct i2c_client * client);
int ft5x02_get_ic_param(struct i2c_client * client);
int ft5x02_Get_Param_From_Ini(char *config_name);

//extern int ft5402_Init_IC_Param(struct i2c_client * client);
//extern int ft5402_get_ic_param(struct i2c_client * client);
#endif

static struct i2c_client *this_client;

struct ts_event {
    u16 au16_x[CFG_MAX_TOUCH_POINTS];              //x coordinate
    u16 au16_y[CFG_MAX_TOUCH_POINTS];              //y coordinate
    u8  au8_touch_event[CFG_MAX_TOUCH_POINTS];     //touch event:  0 -- down; 1-- contact; 2 -- contact
    u8  au8_finger_id[CFG_MAX_TOUCH_POINTS];       //touch ID
    u16	pressure;
    u8  touch_point;
};


struct ft5x0x_ts_data {
    struct input_dev	*input_dev;
    struct ts_event		event;
    struct work_struct 	pen_event_work;
    struct workqueue_struct *ts_workqueue;
#ifdef CONFIG_HAS_EARLYSUSPEND
    struct early_suspend	early_suspend;
#endif
    int irq;
};


#if CFG_SUPPORT_TOUCH_KEY
int tsp_keycodes[CFG_NUMOFKEYS] ={

    KEY_MENU,
    KEY_HOME,
    KEY_BACK,
    KEY_SEARCH
};

char *tsp_keyname[CFG_NUMOFKEYS] ={

    "Menu",
    "Home",
    "Back",
    "Search"
};

static bool tsp_keystatus[CFG_NUMOFKEYS];
#endif
/***********************************************************************************************
Name	:	ft5x0x_i2c_rxdata 

Input	:	*rxdata
 *length

Output	:	ret

function	:	

 ***********************************************************************************************/
static int ft5x0x_i2c_rxdata(char *rxdata, int length)
{
    int ret;

    struct i2c_msg msgs[] = {
        {
            .addr	= this_client->addr,
            .flags	= 0,
            .len	= 1,
            .buf	= rxdata,
        },
        {
            .addr	= this_client->addr,
            .flags	= I2C_M_RD,
            .len	= length,
            .buf	= rxdata,
        },
    };

    //msleep(1);
    ret = i2c_transfer(this_client->adapter, msgs, 2);
    if (ret < 0)
        pr_err("msg %s i2c read error: %d\n", __func__, ret);

    return ret;
}
/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static int ft5x0x_i2c_txdata(char *txdata, int length)
{
    int ret;

    struct i2c_msg msg[] = {
        {
            .addr	= this_client->addr,
            .flags	= 0,
            .len	= length,
            .buf	= txdata,
        },
    };

    //msleep(1);
    ret = i2c_transfer(this_client->adapter, msg, 1);
    if (ret < 0)
        pr_err("%s i2c write error: %d, slave_addr:0x%x\n", __func__, ret, this_client->addr);

    return ret;
}
/***********************************************************************************************
Name	:	 ft5x0x_write_reg

Input	:	addr -- address
para -- parameter

Output	:	

function	:	write register of ft5x0x

 ***********************************************************************************************/
static int ft5x0x_write_reg(u8 addr, u8 para)
{
    u8 buf[3];
    int ret = -1;

    buf[0] = addr;
    buf[1] = para;
    ret = ft5x0x_i2c_txdata(buf, 2);
    if (ret < 0) {
        pr_err("write reg failed! %#x ret: %d", buf[0], ret);
        return -1;
    }

    return 0;
}


/***********************************************************************************************
Name	:	ft5x0x_read_reg 

Input	:	addr
pdata

Output	:	

function	:	read register of ft5x0x

 ***********************************************************************************************/
static int ft5x0x_read_reg(u8 addr, u8 *pdata)
{
    int ret;
    u8 buf[2];
    struct i2c_msg msgs[2];

    //
    buf[0] = addr;    //register address

    msgs[0].addr = this_client->addr;
    msgs[0].flags = 0;
    msgs[0].len = 1;
    msgs[0].buf = buf;
    msgs[1].addr = this_client->addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len = 1;
    msgs[1].buf = buf;

    ret = i2c_transfer(this_client->adapter, msgs, 2);
    if (ret < 0)
        pr_err("msg %s i2c read error: %d\n", __func__, ret);

    *pdata = buf[0];
    return ret;

}
#if 1
//add by LeeSheen
int ft5x02_i2c_Read(struct i2c_client *client,  char * writebuf, int writelen, 
        char *readbuf, int readlen)
{
    int ret;

    if(writelen > 0)
    {
        struct i2c_msg msgs[] = {
            {
                .addr	= client->addr,
                .flags	= 0,
                .len	= writelen,
                .buf	= writebuf,
            },
            {
                .addr	= client->addr,
                .flags	= I2C_M_RD,
                .len	= readlen,
                .buf	= readbuf,
            },
        };
        ret = i2c_transfer(client->adapter, msgs, 2);
        if (ret < 0)
            pr_err("function:%s. i2c read error: %d\n", __func__, ret);
    }
    else
    {
        struct i2c_msg msgs[] = {
            {
                .addr	= client->addr,
                .flags	= I2C_M_RD,
                .len	= readlen,
                .buf	= readbuf,
            },
        };
        ret = i2c_transfer(client->adapter, msgs, 1);
        if (ret < 0)
            pr_err("function:%s. i2c read error: %d\n", __func__, ret);
    }
    return ret;
}
/*
 *write data by i2c 
 */
int ft5x02_i2c_Write(struct i2c_client *client, char *writebuf, int writelen)
{
    int ret;

    struct i2c_msg msg[] = {
        {
            .addr	= client->addr,
            .flags	= 0,
            .len	= writelen,
            .buf	= writebuf,
        },
    };
    //msleep(1);
    ret = i2c_transfer(client->adapter, msg, 1);
    if (ret < 0)
        pr_err("%s i2c write error: %d, slave_addr:0x%x\n", __func__, ret, client->addr);

    return ret;
}

int ft5x02_write_reg(struct i2c_client * client, u8 regaddr, u8 regvalue)
{
    unsigned char buf[2] = {0};
    buf[0] = regaddr;
    buf[1] = regvalue;

    return ft5x02_i2c_Write(client, buf, sizeof(buf));
}

int ft5x02_read_reg(struct i2c_client * client, u8 regaddr, u8 * regvalue)
{
    return ft5x02_i2c_Read(client, &regaddr, 1, regvalue, 1);
}




#endif 

/***********************************************************************************************
Name	:	 ft5x0x_read_fw_ver

Input	:	 void


Output	:	 firmware version 	

function	:	 read TP firmware version

 ***********************************************************************************************/
static unsigned char ft5x0x_read_fw_ver(void)
{
    unsigned char ver;
    ft5x0x_read_reg(FT5X0X_REG_FIRMID, &ver);
    return(ver);
}

#if 1  //upgrade related
typedef enum
{
    ERR_OK,
    ERR_MODE,
    ERR_READID,
    ERR_ERASE,
    ERR_STATUS,
    ERR_ECC,
    ERR_DL_ERASE_FAIL,
    ERR_DL_PROGRAM_FAIL,
    ERR_DL_VERIFY_FAIL
}E_UPGRADE_ERR_TYPE;

typedef unsigned char         FTS_BYTE;     //8 bit
typedef unsigned short        FTS_WORD;    //16 bit
typedef unsigned int          FTS_DWRD;    //16 bit
typedef unsigned char         FTS_BOOL;    //8 bit

typedef struct _FTS_CTP_PROJECT_SETTING_T
{
    unsigned char uc_i2C_addr;             //I2C slave address (8 bit address)
    unsigned char uc_io_voltage;           //IO Voltage 0---3.3v;	1----1.8v
    unsigned char uc_panel_factory_id;     //TP panel factory ID
}FTS_CTP_PROJECT_SETTING_T;

#define FTS_NULL                0x0
#define FTS_TRUE                0x01
#define FTS_FALSE              0x0

#define I2C_CTPM_ADDRESS       (0x70)


void delay_qt_ms(unsigned long  w_ms)
{
    unsigned long i;
    unsigned long j;

    for (i = 0; i < w_ms; i++)
    {
        for (j = 0; j < 1000; j++)
        {
            udelay(1);
        }
    }
}


/*
   [function]: 
callback: read data from ctpm by i2c interface,implemented by special user;
[parameters]:
bt_ctpm_addr[in]    :the address of the ctpm;
pbt_buf[out]        :data buffer;
dw_lenth[in]        :the length of the data buffer;
[return]:
FTS_TRUE     :success;
FTS_FALSE    :fail;
*/
FTS_BOOL i2c_read_interface(FTS_BYTE bt_ctpm_addr, FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
    int ret;

    ret=i2c_master_recv(this_client, pbt_buf, dw_lenth);

    if(ret<=0)
    {
        printk("[FTS]i2c_read_interface error\n");
        return FTS_FALSE;
    }

    return FTS_TRUE;
}

/*
   [function]: 
callback: write data to ctpm by i2c interface,implemented by special user;
[parameters]:
bt_ctpm_addr[in]    :the address of the ctpm;
pbt_buf[in]        :data buffer;
dw_lenth[in]        :the length of the data buffer;
[return]:
FTS_TRUE     :success;
FTS_FALSE    :fail;
*/
FTS_BOOL i2c_write_interface(FTS_BYTE bt_ctpm_addr, FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
    int ret;
    ret=i2c_master_send(this_client, pbt_buf, dw_lenth);
    if(ret<=0)
    {
        printk("[FTS]i2c_write_interface error line = %d, ret = %d\n", __LINE__, ret);
        return FTS_FALSE;
    }

    return FTS_TRUE;
}

/*
   [function]: 
   send a command to ctpm.
   [parameters]:
   btcmd[in]        :command code;
   btPara1[in]    :parameter 1;    
   btPara2[in]    :parameter 2;    
   btPara3[in]    :parameter 3;    
   num[in]        :the valid input parameter numbers, if only command code needed and no parameters followed,then the num is 1;    
   [return]:
FTS_TRUE    :success;
FTS_FALSE    :io fail;
*/
FTS_BOOL cmd_write(FTS_BYTE btcmd,FTS_BYTE btPara1,FTS_BYTE btPara2,FTS_BYTE btPara3,FTS_BYTE num)
{
    FTS_BYTE write_cmd[4] = {0};

    write_cmd[0] = btcmd;
    write_cmd[1] = btPara1;
    write_cmd[2] = btPara2;
    write_cmd[3] = btPara3;
    return i2c_write_interface(I2C_CTPM_ADDRESS, write_cmd, num);
}

/*
   [function]: 
   write data to ctpm , the destination address is 0.
   [parameters]:
   pbt_buf[in]    :point to data buffer;
   bt_len[in]        :the data numbers;    
   [return]:
FTS_TRUE    :success;
FTS_FALSE    :io fail;
*/
FTS_BOOL byte_write(FTS_BYTE* pbt_buf, FTS_DWRD dw_len)
{

    return i2c_write_interface(I2C_CTPM_ADDRESS, pbt_buf, dw_len);
}

/*
   [function]: 
   read out data from ctpm,the destination address is 0.
   [parameters]:
   pbt_buf[out]    :point to data buffer;
   bt_len[in]        :the data numbers;    
   [return]:
FTS_TRUE    :success;
FTS_FALSE    :io fail;
*/
FTS_BOOL byte_read(FTS_BYTE* pbt_buf, FTS_BYTE bt_len)
{
    return i2c_read_interface(I2C_CTPM_ADDRESS, pbt_buf, bt_len);
}


/*
   [function]: 
   burn the FW to ctpm.
   [parameters]:(ref. SPEC)
   pbt_buf[in]    :point to Head+FW ;
   dw_lenth[in]:the length of the FW + 6(the Head length);    
   bt_ecc[in]    :the ECC of the FW
   [return]:
ERR_OK        :no error;
ERR_MODE    :fail to switch to UPDATE mode;
ERR_READID    :read id fail;
ERR_ERASE    :erase chip fail;
ERR_STATUS    :status error;
ERR_ECC        :ecc error.
*/


#define    FTS_PACKET_LENGTH        128

static unsigned char CTPM_FW[]=
{
    //#include "ft_app.i"
};

E_UPGRADE_ERR_TYPE  fts_ctpm_fw_upgrade(FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
    FTS_BYTE reg_val[2] = {0};
    FTS_DWRD i = 0;

    FTS_DWRD  packet_number;
    FTS_DWRD  j;
    FTS_DWRD  temp;
    FTS_DWRD  lenght;
    FTS_BYTE  packet_buf[FTS_PACKET_LENGTH + 6];
    FTS_BYTE  auc_i2c_write_buf[10];
    FTS_BYTE bt_ecc;
    int      i_ret;

    /*********Step 1:Reset  CTPM *****/
    /*write 0xaa to register 0xfc*/
    ft5x0x_write_reg(0xfc,0xaa);
    delay_qt_ms(50);
    /*write 0x55 to register 0xfc*/
    ft5x0x_write_reg(0xfc,0x55);
    printk("[FTS] Step 1: Reset CTPM test\n");

    delay_qt_ms(30);   


    /*********Step 2:Enter upgrade mode *****/
    auc_i2c_write_buf[0] = 0x55;
    auc_i2c_write_buf[1] = 0xaa;
    do
    {
        i ++;
        i_ret = ft5x0x_i2c_txdata(auc_i2c_write_buf, 2);
        delay_qt_ms(5);
    }while(i_ret <= 0 && i < 5 );

    /*********Step 3:check READ-ID***********************/        
    cmd_write(0x90,0x00,0x00,0x00,4);
    byte_read(reg_val,2);
    if (reg_val[0] == 0x79 && reg_val[1] == 0x3)
    {
        printk("[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
    }
    else
    {
        return ERR_READID;
        //i_is_new_protocol = 1;
    }

    cmd_write(0xcd,0x0,0x00,0x00,1);
    byte_read(reg_val,1);
    printk("[FTS] bootloader version = 0x%x\n", reg_val[0]);

    /*********Step 4:erase app and panel paramenter area ********************/
    cmd_write(0x61,0x00,0x00,0x00,1);  //erase app area
    delay_qt_ms(1500); 
    cmd_write(0x63,0x00,0x00,0x00,1);  //erase panel parameter area
    delay_qt_ms(100);
    printk("[FTS] Step 4: erase. \n");

    /*********Step 5:write firmware(FW) to ctpm flash*********/
    bt_ecc = 0;
    printk("[FTS] Step 5: start upgrade. \n");
    dw_lenth = dw_lenth - 8;
    packet_number = (dw_lenth) / FTS_PACKET_LENGTH;
    packet_buf[0] = 0xbf;
    packet_buf[1] = 0x00;
    for (j=0;j<packet_number;j++)
    {
        temp = j * FTS_PACKET_LENGTH;
        packet_buf[2] = (FTS_BYTE)(temp>>8);
        packet_buf[3] = (FTS_BYTE)temp;
        lenght = FTS_PACKET_LENGTH;
        packet_buf[4] = (FTS_BYTE)(lenght>>8);
        packet_buf[5] = (FTS_BYTE)lenght;

        for (i=0;i<FTS_PACKET_LENGTH;i++)
        {
            packet_buf[6+i] = pbt_buf[j*FTS_PACKET_LENGTH + i]; 
            bt_ecc ^= packet_buf[6+i];
        }

        byte_write(&packet_buf[0],FTS_PACKET_LENGTH + 6);
        delay_qt_ms(FTS_PACKET_LENGTH/6 + 1);
        if ((j * FTS_PACKET_LENGTH % 1024) == 0)
        {
            printk("[FTS] upgrade the 0x%x th byte.\n", ((unsigned int)j) * FTS_PACKET_LENGTH);
        }
    }

    if ((dw_lenth) % FTS_PACKET_LENGTH > 0)
    {
        temp = packet_number * FTS_PACKET_LENGTH;
        packet_buf[2] = (FTS_BYTE)(temp>>8);
        packet_buf[3] = (FTS_BYTE)temp;

        temp = (dw_lenth) % FTS_PACKET_LENGTH;
        packet_buf[4] = (FTS_BYTE)(temp>>8);
        packet_buf[5] = (FTS_BYTE)temp;

        for (i=0;i<temp;i++)
        {
            packet_buf[6+i] = pbt_buf[ packet_number*FTS_PACKET_LENGTH + i]; 
            bt_ecc ^= packet_buf[6+i];
        }

        byte_write(&packet_buf[0],temp+6);    
        delay_qt_ms(20);
    }

    //send the last six byte
    for (i = 0; i<6; i++)
    {
        temp = 0x6ffa + i;
        packet_buf[2] = (FTS_BYTE)(temp>>8);
        packet_buf[3] = (FTS_BYTE)temp;
        temp =1;
        packet_buf[4] = (FTS_BYTE)(temp>>8);
        packet_buf[5] = (FTS_BYTE)temp;
        packet_buf[6] = pbt_buf[ dw_lenth + i]; 
        bt_ecc ^= packet_buf[6];

        byte_write(&packet_buf[0],7);  
        delay_qt_ms(20);
    }

    /*********Step 6: read out checksum***********************/
    /*send the opration head*/
    cmd_write(0xcc,0x00,0x00,0x00,1);
    byte_read(reg_val,1);
    printk("[FTS] Step 6:  ecc read 0x%x, new firmware 0x%x. \n", reg_val[0], bt_ecc);
    if(reg_val[0] != bt_ecc)
    {
        return ERR_ECC;
    }

    /*********Step 7: reset the new FW***********************/
    cmd_write(0x07,0x00,0x00,0x00,1);

    msleep(300);  //make sure CTP startup normally

    return ERR_OK;
}

int fts_ctpm_auto_clb(void)
{
    unsigned char uc_temp;
    unsigned char i ;

    printk("[FTS] start auto CLB.\n");
    msleep(200);
    ft5x0x_write_reg(0, 0x40);  
    delay_qt_ms(100);   //make sure already enter factory mode
    ft5x0x_write_reg(2, 0x4);  //write command to start calibration
    delay_qt_ms(300);
    for(i=0;i<100;i++)
    {
        ft5x0x_read_reg(0,&uc_temp);
        if ( ((uc_temp&0x70)>>4) == 0x0)  //return to normal mode, calibration finish
        {
            break;
        }
        delay_qt_ms(200);
        printk("[FTS] waiting calibration %d\n",i);

    }
    printk("[FTS] calibration OK.\n");

    msleep(300);
    ft5x0x_write_reg(0, 0x40);  //goto factory mode
    delay_qt_ms(100);   //make sure already enter factory mode
    ft5x0x_write_reg(2, 0x5);  //store CLB result
    delay_qt_ms(300);
    ft5x0x_write_reg(0, 0x0); //return to normal mode 
    msleep(300);
    printk("[FTS] store CLB result OK.\n");
    return 0;
}

int fts_ctpm_fw_upgrade_with_i_file(void)
{
    FTS_BYTE*     pbt_buf = FTS_NULL;
    int i_ret;

    //=========FW upgrade========================*/
    pbt_buf = CTPM_FW;
    /*call the upgrade function*/
    i_ret =  fts_ctpm_fw_upgrade(pbt_buf,sizeof(CTPM_FW));
    if (i_ret != 0)
    {
        printk("[FTS] upgrade failed i_ret = %d.\n", i_ret);
        //error handling ...
        //TBD
    }
    else
    {
        printk("[FTS] upgrade successfully.\n");
        fts_ctpm_auto_clb();  //start auto CLB
        fts_ctpm_auto_clb();  //start auto CLB
    }

    return i_ret;
}

unsigned char fts_ctpm_get_i_file_ver(void)
{
    unsigned int ui_sz;
    ui_sz = sizeof(CTPM_FW);
    if (ui_sz > 2)
    {
        return CTPM_FW[ui_sz - 2];
    }
    else
    {
        //TBD, error handling?
        return 0xff; //default value
    }
}

#define    FTS_SETTING_BUF_LEN        128

//update project setting
//only update these settings for COB project, or for some special case
int fts_ctpm_update_project_setting(void)
{
    unsigned char uc_i2c_addr;             //I2C slave address (8 bit address)
    unsigned char uc_io_voltage;           //IO Voltage 0---3.3v;	1----1.8v
    unsigned char uc_panel_factory_id;     //TP panel factory ID

    unsigned char buf[FTS_SETTING_BUF_LEN];
    FTS_BYTE reg_val[2] = {0};
    FTS_BYTE  auc_i2c_write_buf[10];
    FTS_BYTE  packet_buf[FTS_SETTING_BUF_LEN + 6];
    FTS_DWRD i = 0;
    int      i_ret;

    uc_i2c_addr = 0x70;
    uc_io_voltage = 0x0;
    uc_panel_factory_id = 0x5a;

    /*********Step 1:Reset  CTPM *****/
    /*write 0xaa to register 0xfc*/
    ft5x0x_write_reg(0xfc,0xaa);
    delay_qt_ms(50);
    /*write 0x55 to register 0xfc*/
    ft5x0x_write_reg(0xfc,0x55);
    printk("[FTS] Step 1: Reset CTPM test\n");

    delay_qt_ms(30);   

    /*********Step 2:Enter upgrade mode *****/
    auc_i2c_write_buf[0] = 0x55;
    auc_i2c_write_buf[1] = 0xaa;
    do
    {
        i ++;
        i_ret = ft5x0x_i2c_txdata(auc_i2c_write_buf, 2);
        delay_qt_ms(5);
    }while(i_ret <= 0 && i < 5 );

    /*********Step 3:check READ-ID***********************/        
    cmd_write(0x90,0x00,0x00,0x00,4);
    byte_read(reg_val,2);
    if (reg_val[0] == 0x79 && reg_val[1] == 0x3)
    {
        printk("[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
    }
    else
    {
        return ERR_READID;
    }

    cmd_write(0xcd,0x0,0x00,0x00,1);
    byte_read(reg_val,1);
    printk("bootloader version = 0x%x\n", reg_val[0]);


    /* --------- read current project setting  ---------- */
    //set read start address
    buf[0] = 0x3;
    buf[1] = 0x0;
    buf[2] = 0x78;
    buf[3] = 0x0;
    byte_write(buf, 4);
    byte_read(buf, FTS_SETTING_BUF_LEN);

    printk("[FTS] old setting: uc_i2c_addr = 0x%x, uc_io_voltage = %d, uc_panel_factory_id = 0x%x\n",
            buf[0],  buf[2], buf[4]);
    for (i = 0; i < FTS_SETTING_BUF_LEN; i++)
    {
        if (i % 16 == 0)     printk("\n");
        printk("0x%x, ", buf[i]);

    }
    printk("\n");

    /*--------- Step 4:erase project setting --------------*/
    cmd_write(0x62,0x00,0x00,0x00,1);
    delay_qt_ms(100);

    /*----------  Set new settings ---------------*/
    buf[0] = uc_i2c_addr;
    buf[1] = ~uc_i2c_addr;
    buf[2] = uc_io_voltage;
    buf[3] = ~uc_io_voltage;
    buf[4] = uc_panel_factory_id;
    buf[5] = ~uc_panel_factory_id;
    packet_buf[0] = 0xbf;
    packet_buf[1] = 0x00;
    packet_buf[2] = 0x78;
    packet_buf[3] = 0x0;
    packet_buf[4] = 0;
    packet_buf[5] = FTS_SETTING_BUF_LEN;
    for (i = 0; i < FTS_SETTING_BUF_LEN; i++)
    {
        packet_buf[6 + i] = buf[i];
        if (i % 16 == 0)     printk("\n");
        printk("0x%x, ", buf[i]);
    }
    printk("\n");
    byte_write(&packet_buf[0],FTS_SETTING_BUF_LEN + 6);
    delay_qt_ms(100);

    /********* reset the new FW***********************/
    cmd_write(0x07,0x00,0x00,0x00,1);

    msleep(200);

    return 0;

}



#if CFG_SUPPORT_AUTO_UPG

int fts_ctpm_auto_upg(void)
{
    unsigned char uc_host_fm_ver;
    unsigned char uc_tp_fm_ver;
    int           i_ret;

    uc_tp_fm_ver = ft5x0x_read_fw_ver();
    uc_host_fm_ver = fts_ctpm_get_i_file_ver();
    if ( uc_tp_fm_ver == 0xa6  ||   //the firmware in touch panel maybe corrupted
            uc_tp_fm_ver < uc_host_fm_ver //the firmware in host flash is new, need upgrade
       )
    {
        msleep(100);
        printk("[FTS] uc_tp_fm_ver = 0x%x, uc_host_fm_ver = 0x%x\n",
                uc_tp_fm_ver, uc_host_fm_ver);
        i_ret = fts_ctpm_fw_upgrade_with_i_file();    
        if (i_ret == 0)
        {
            msleep(300);
            uc_host_fm_ver = fts_ctpm_get_i_file_ver();
            printk("[FTS] upgrade to new version 0x%x\n", uc_host_fm_ver);
        }
        else
        {
            printk("[FTS] upgrade failed ret=%d.\n", i_ret);
        }
    }

    return 0;
}

#endif

#endif


/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static void ft5x0x_ts_release(void)
{
    struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
    input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
    input_sync(data->input_dev);
    printk("in release\n");
}

#if 0
/* LeeSheen add for calc 800x480 to 1024x600*/
static int my_div(int num, int div)
{
    int time = 0;
    while(num >= div) {
        num -= div;
        time++;
    }
    return time;
}
#endif

//read touch point information
static int ft5x0x_read_data(void)
{
    struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
    struct ts_event *event = &data->event;
    u8 buf[CFG_POINT_READ_BUF] = {0};
    int ret = -1;
    int i;

    ret = ft5x0x_i2c_rxdata(buf, CFG_POINT_READ_BUF);
    if (ret < 0) {
        printk("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
        return ret;
    }
    memset(event, 0, sizeof(struct ts_event));
    event->touch_point = buf[2] & 0x07; 

    //printk("-----------touch_point is %d----------------\n", event->touch_point);

    if (event->touch_point > CFG_MAX_TOUCH_POINTS)
    {
        event->touch_point = CFG_MAX_TOUCH_POINTS;
    }

    for (i = 0; i < event->touch_point; i++)
    {
        event->au16_y[i] = 600 - ((s16)(buf[3 + 6*i] & 0x0F)<<8 | (s16)buf[4 + 6*i]);//x->y
        event->au16_x[i] = (((s16)(buf[5 + 6*i] & 0x0F)<<8 | (s16)buf[6 + 6*i]));//y->x

        event->au8_touch_event[i] = buf[0x3 + 6*i] >> 6;
        event->au8_finger_id[i] = (buf[5 + 6*i])>>4;

        //printk("in read data: buf[3] = %d, buf[4] = %d\n", buf[3], buf[4]);
        //printk("after in read data: x = %d, y = %d\n", event->au16_x[i], event->au16_y[i]);
    }

    event->pressure = 200;

    return 0;
}

/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/


#if CFG_SUPPORT_TOUCH_KEY
int ft5x0x_touch_key_process(struct input_dev *dev, int x, int y, int touch_event)
{
    int i;
    int key_id;

    if ( y < 517&&y > 497)
    {
        key_id = 1;
    }
    else if ( y < 367&&y > 347)
    {
        key_id = 0;
    }

    else if ( y < 217&&y > 197)
    {
        key_id = 2;
    }  
    else if (y < 67&&y > 47)
    {
        key_id = 3;
    }
    else
    {
        key_id = 0xf;
    }

    for(i = 0; i <CFG_NUMOFKEYS; i++ )
    {
        if(tsp_keystatus[i])
        {
            input_report_key(dev, tsp_keycodes[i], 0);

            printk("[FTS] %s key is release. Keycode : %d\n", tsp_keyname[i], tsp_keycodes[i]);

            tsp_keystatus[i] = KEY_RELEASE;
        }
        else if( key_id == i )
        {
            if( touch_event == 0)                                  // detect
            {
                input_report_key(dev, tsp_keycodes[i], 1);
                printk( "[FTS] %s key is pressed. Keycode : %d\n", tsp_keyname[i], tsp_keycodes[i]);
                tsp_keystatus[i] = KEY_PRESS;
            }
        }
    }
    return 0;

}    
#endif

static void ft5x0x_report_value(void)
{
    struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
    struct ts_event *event = &data->event;
    int i;

    for (i  = 0; i < event->touch_point; i++)
    {

        //modify by leesheen
        //printk("---ft5x0x_report_value---x is %d, y is %d\n", event->au16_x[i], event->au16_y[i]);
        if (event->au16_x[i] < SCREEN_MAX_X && event->au16_y[i] < SCREEN_MAX_Y)
            //if (event->au16_x[i] < SCREEN_MAX_X && event->au16_y[i] < SCREEN_MAX_Y)
            // LCD view area
        {
            //printk("---ft5x0x_report_value---x is %d, y is %d\n", event->au16_x[i], event->au16_y[i]);
#if 1
            input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->au16_x[i]);
            input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->au16_y[i]);
#else
            input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->au16_y[i]);
            input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->au16_x[i]);
#endif
            input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
            input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->au8_finger_id[i]);
            if (event->au8_touch_event[i]== 0 || event->au8_touch_event[i] == 2)
            {
                //printk("report abs in if, touch event = %d\n", event->au8_touch_event[i]);
                input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
            }
            else
            {
                //printk("report abs in else\n");
                input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
            }
        }
        else //maybe the touch key area
        {
#if CFG_SUPPORT_TOUCH_KEY
            if (event->au16_x[i] >= SCREEN_MAX_X)
            {
                ft5x0x_touch_key_process(data->input_dev, event->au16_x[i], event->au16_y[i], event->au8_touch_event[i]);
            }
#endif
        }


        input_mt_sync(data->input_dev);
        //printk("touch_point != 0\n");
    }

    if (event->touch_point == 0) {
        //printk("touch_point == 0\n");
        input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
        input_mt_sync(data->input_dev);
    } 
    //printk("==========report key, touch_point = %d\n", event->touch_point);
    input_report_key(data->input_dev, BTN_TOUCH, (event->touch_point > 0));
    input_sync(data->input_dev);

#if 0
    if (event->touch_point == 0) {
        ft5x0x_ts_release();
        return ; 
    }
#endif


}	/*end ft5x0x_report_value*/


/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static void ft5x0x_ts_pen_irq_work(struct work_struct *work)
{
    int ret = -1;
#if 1
    ret = ft5x0x_read_data();	
    if (ret == 0) {	
        ft5x0x_report_value();
    }
#else
    ret = ft5x0x_read_data();	
    if (ret == 0 && lcd_flag == 0) {	
        ft5x0x_report_value();
    }else if(lcd_flag == 1){
        lcd_flag = 0 ;//如果关屏了，则开屏
        msleep(500);
    }
#endif
    enable_irq(ft5x06_irq);
    //printk("-------------ft5x0x_ts_interrupt----enable_irq------%d------\n",ft5x06_irq);
}
/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static irqreturn_t ft5x0x_ts_interrupt(int irq, void *dev_id)
{
    int ret = -1;
    struct ft5x0x_ts_data *ft5x0x_ts = dev_id;
#if 1
    ret = ft5x0x_read_data();	
    ft5x0x_report_value();

#else
    disable_irq_nosync(ft5x0x_ts->irq);
    if (!work_pending(&ft5x0x_ts->pen_event_work)) {
        //printk("----ft5x0x_ts_interrupt---work_pending---\n");
        queue_work(ft5x0x_ts->ts_workqueue, &ft5x0x_ts->pen_event_work);
    }
#endif
    return IRQ_HANDLED;
}
#ifdef CONFIG_HAS_EARLYSUSPEND
/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static void ft5x0x_ts_suspend(struct early_suspend *handler)
{
    //	struct ft5x0x_ts_data *ts;
    //	ts =  container_of(handler, struct ft5x0x_ts_data, early_suspend);

    printk("==ft5x0x_ts_suspend=\n");
    //	disable_irq(this_client->irq);
    //	disable_irq(IRQ_EINT(6));
    //	cancel_work_sync(&ts->pen_event_work);
    //	flush_workqueue(ts->ts_workqueue);
    // ==set mode ==, 
    //    	ft5x0x_set_reg(FT5X0X_REG_PMODE, PMODE_HIBERNATE);
}
/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static void ft5x0x_ts_resume(struct early_suspend *handler)
{
    printk("==ft5x0x_ts_resume=\n");
    // wake the mode
    //	__gpio_as_output(GPIO_FT5X0X_WAKE);		
    //	__gpio_clear_pin(GPIO_FT5X0X_WAKE);		//set wake = 0,base on system
    //	 msleep(100);
    //	__gpio_set_pin(GPIO_FT5X0X_WAKE);			//set wake = 1,base on system
    //	msleep(100);
    //	enable_irq(this_client->irq);
    //	enable_irq(IRQ_EINT(6));
}
#endif  //CONFIG_HAS_EARLYSUSPEND

/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
    static int 
ft5x0x_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct ft5x0x_ts_data *ft5x0x_ts;
    struct input_dev *input_dev;
    int err = 0;
    unsigned char uc_reg_value; 
#if CFG_SUPPORT_TOUCH_KEY
    int i;
#endif
    int j = 0;

    printk("[FTS] ft5x0x_ts_probe, driver version is %s.\n", CFG_FTS_CTP_DRIVER_VERSION);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        err = -ENODEV;
        goto exit_check_functionality_failed;
    }

    ft5x0x_ts = kzalloc(sizeof(*ft5x0x_ts), GFP_KERNEL);
    if (!ft5x0x_ts)	{
        err = -ENOMEM;
        goto exit_alloc_data_failed;
    }

 //gcc************
    INIT_WORK(&ft5x0x_ts->pen_event_work, ft5x0x_ts_pen_irq_work);

    ft5x0x_ts->ts_workqueue = create_singlethread_workqueue(dev_name(&client->dev));
    if (!ft5x0x_ts->ts_workqueue) {
        err = -ESRCH;
        goto exit_create_singlethread;
    }

#if 0
    err = request_irq(IRQ_EINT(14), ft5x0x_ts_interrupt, IRQF_TRIGGER_FALLING, "ft5x0x_ts", ft5x0x_ts);
    if (err < 0) {
        dev_err(&client->dev, "ft5x0x_probe: request irq failed\n");
        goto exit_irq_request_failed;
    }


    disable_irq(IRQ_EINT(14));
#else
    //writel((readl(CHG_PULL1) | 0x00000050), CHG_PULL1);
    /* for zing */

#define pen_irq_pin              54
#define wake_pin              57
    if ((err = gpio_request(wake_pin, "ft5x0x_ts")) < 0)
    {
        printk(KERN_ERR "unable to request gpio\n");
        goto exit_irq_request_failed;
    }
    gpio_direction_output(wake_pin, 0);

    if ((err = gpio_request(pen_irq_pin, "ft5x0x_ts")) < 0)
    {
        printk(KERN_ERR "unable to request gpio\n");
        goto exit_irq_request_failed;
    }
    gpio_direction_input(pen_irq_pin);

    /*************/
    //ft5x0x_ts->irq = IRQ_EINT(4);
    ft5x0x_ts->irq = gpio_to_irq(pen_irq_pin);
    ft5x06_irq = ft5x0x_ts->irq;
    irq_set_irq_type(ft5x0x_ts->irq, IRQ_TYPE_EDGE_FALLING);
    //set_irq_type(ft5x0x_ts->irq, IRQ_TYPE_LEVEL_LOW);

    this_client = client;
    i2c_set_clientdata(client, ft5x0x_ts);

    err = request_threaded_irq(ft5x0x_ts->irq, NULL, ft5x0x_ts_interrupt,
                                     IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                                     "ft5x0x_ts", ft5x0x_ts);
   // err = request_irq(ft5x0x_ts->irq, ft5x0x_ts_interrupt, IRQF_TRIGGER_FALLING, "ft5x0x_ts", ft5x0x_ts);
    if (err < 0) {	
        printk("------ft----%s: irq %d busy?\n", __func__, ft5x0x_ts->irq);  
        goto exit_irq_request_failed;	
    }else if(err == 0){
     //   disable_irq_nosync(ft5x0x_ts->irq);
        printk("%s:request_irq irq=%d success!!!\n", __func__, ft5x0x_ts->irq);
    } //gcc***********
#endif
    input_dev = input_allocate_device();
    if (!input_dev) {
        err = -ENOMEM;
        dev_err(&client->dev, "failed to allocate input device\n");
        goto exit_input_dev_alloc_failed;
    }

    ft5x0x_ts->input_dev = input_dev;

    set_bit(EV_SYN, input_dev->evbit);
    set_bit(EV_ABS, input_dev->evbit);
    set_bit(EV_KEY, input_dev->evbit);

    set_bit(ABS_MT_TRACKING_ID, input_dev->absbit);
    set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
    set_bit(ABS_MT_POSITION_X, input_dev->absbit);
    set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
    set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);
    //add by leesheen

    input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) ;
    input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
    input_dev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE);

    //printk("SCREEN_MAX_X = %d, SCREEN_MAX_Y = %d\n", SCREEN_MAX_X, SCREEN_MAX_Y);

    input_set_abs_params(input_dev,
            ABS_MT_POSITION_X, 0, 1024, 0, 0);
    input_set_abs_params(input_dev,
            ABS_MT_POSITION_Y, 0, 600, 0, 0);
    input_set_abs_params(input_dev,
            ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
    input_set_abs_params(input_dev,
            ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(input_dev,
            ABS_MT_TRACKING_ID, 0, 5, 0, 0);


    //set_bit(EV_KEY, input_dev->evbit);
    //set_bit(EV_ABS, input_dev->evbit);

#if CFG_SUPPORT_TOUCH_KEY
    //setup key code area
    set_bit(EV_SYN, input_dev->evbit);
    set_bit(BTN_TOUCH, input_dev->keybit);
    input_dev->keycode = tsp_keycodes;
    for(i = 0; i < CFG_NUMOFKEYS; i++)
    {
        input_set_capability(input_dev, EV_KEY, ((int*)input_dev->keycode)[i]);
        tsp_keystatus[i] = KEY_RELEASE;
    }
#endif

    input_dev->name		= FT5X0X_NAME;		//dev_name(&client->dev)
    input_dev->id.bustype = BUS_I2C;
    input_dev->id.vendor = 0x12FA;
    input_dev->id.product = 0x2143;
    input_dev->id.version = 0x0100;

    err = input_register_device(input_dev);
    if (err) {
        dev_err(&client->dev,
                "ft5x0x_ts_probe: failed to register input device: %s\n",
                dev_name(&client->dev));
        goto exit_input_register_device_failed;
    }

#ifdef CONFIG_HAS_EARLYSUSPEND
    //printk("==register_early_suspend =\n");
    ft5x0x_ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
    ft5x0x_ts->early_suspend.suspend = ft5x0x_ts_suspend;
    ft5x0x_ts->early_suspend.resume	= ft5x0x_ts_resume;
    register_early_suspend(&ft5x0x_ts->early_suspend);
#endif

#if 1
    //add by leesheen
    //ft5x02_read_reg(client, 0xa3, &chip_type);
    //printk("======chip_type = 0x%02x========\n", chip_type);
    //mdelay(250);
    chip_type = 0x02;
#ifdef UPDATE_BY_I_FILE
#ifdef  USE_H_FILE //use h file 	
    if(chip_type == 0x02) //chip_type = ft5202 5302 programed
    {
        mdelay(250);
        while(j < UPDATE_TRY_TO_WRITE)
        {
            uc_reg_value = ft5x0x_read_fw_ver();
            printk("count = %d\n", j);
            ft5x02_Init_IC_Param(client);
            mdelay(2);	
            j++;
        }		
        //	mdelay(50);
        //	ft5x02_write_reg(this_client, 0x00, 0x40);
        //	mdelay(150);
        //	ft5x02_write_reg(this_client, 0x00, 0x00);
        //	mdelay(150);	
    } 
    else if(chip_type == 0x03) //chip_type = ft5402
    {
        mdelay(350);
        //	ft5402_Init_IC_Param(client);
        mdelay(50);
#ifdef SYSFS_DEBUG
        //	ft5402_get_ic_param(client);
#endif
        mdelay(50);
        ft5x02_write_reg(client, 0x00, 0x40);//change mode
        mdelay(150);
        ft5x02_write_reg(client, 0x00, 0x00);
        mdelay(150);
    }
    else//chip_type = ft5x02 noprogramed
#endif 
    {
        // #ifdef CONFIG_SUPPORT_FTS_CTP_UPG
        // msleep(250);
        // fts_ctpm_fw_upgrade_with_i_file();
        // #endif
#ifdef CONFIG_SUPPORT_FTS_CTP_UPG  //use .i file
        msleep(250);
        ft5x02_ctpm_fw_upgrade_with_i_file(client);
        printk("CONFIG_SUPPORT_FTS_CTP_UPG\n");
#endif 

        //ft5x02_read_reg(client, 0xa6, &chip_type);
        printk("======REG:A6= 0x%02x========\n", chip_type);

#ifdef SYSFS_DEBUG
        ft5x02_get_ic_param(client);
        ft5x02_create_sysfs_debug(client);
#endif

#ifdef FTS_APK_DEBUG
        ft5x02_create_apk_debug_channel(client);
#endif	
    }

#if 0
    if (ft5x02_Get_Param_From_Ini(FT5X02_CONFIG_NAME) >= 0)
        ft5x02_Init_IC_Param(this_client);
    else
        dev_err(&this_client->dev, "[FTS]-------Get ft5x02 param from INI file failed\n");
#endif


#endif	

    //msleep(150);  //make sure CTP already finish startup process
#if 0

    //get some register information
    uc_reg_value = ft5x0x_read_fw_ver();
    printk("[FTS] Firmware version = 0x%x\n", uc_reg_value);
    ft5x0x_read_reg(FT5X0X_REG_PERIODACTIVE, &uc_reg_value);
    printk("[FTS] report rate is %dHz.\n", uc_reg_value * 10);
    ft5x0x_read_reg(FT5X0X_REG_THGROUP, &uc_reg_value);
    printk("[FTS] touch threshold is %d.\n", uc_reg_value * 4);
#endif

#if CFG_SUPPORT_AUTO_UPG
    fts_ctpm_auto_upg();
#endif    

#if CFG_SUPPORT_UPDATE_PROJECT_SETTING
    fts_ctpm_update_project_setting();
#endif
#endif
    gpio_direction_output(wake_pin, 1);
    //gcc************
    //enable_irq(ft5x0x_ts->irq);

    printk("[FTS] FS210 1024x600 TouchScreen driver installed!\n");
    return 0;

exit_input_register_device_failed:
    input_free_device(input_dev);
exit_input_dev_alloc_failed:
    //	free_irq(client->irq, ft5x0x_ts);
    free_irq(ft5x0x_ts->irq, ft5x0x_ts);
exit_irq_request_failed:
    //exit_platform_data_null:
    cancel_work_sync(&ft5x0x_ts->pen_event_work);
    destroy_workqueue(ft5x0x_ts->ts_workqueue);
exit_create_singlethread:
    printk("==singlethread error =\n");
    i2c_set_clientdata(client, NULL);
    kfree(ft5x0x_ts);
exit_alloc_data_failed:
exit_check_functionality_failed:
    return err;
}
/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static int ft5x0x_ts_remove(struct i2c_client *client)
{
    struct ft5x0x_ts_data *ft5x0x_ts;
    printk("==ft5x0x_ts_remove=\n");
    ft5x0x_ts = i2c_get_clientdata(client);
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&ft5x0x_ts->early_suspend);
#endif
    //	free_irq(client->irq, ft5x0x_ts);
    free_irq(ft5x0x_ts->irq, ft5x0x_ts);
    input_unregister_device(ft5x0x_ts->input_dev);
    kfree(ft5x0x_ts);
    cancel_work_sync(&ft5x0x_ts->pen_event_work);
    destroy_workqueue(ft5x0x_ts->ts_workqueue);
    i2c_set_clientdata(client, NULL);
    return 0;
}

static const struct i2c_device_id ft5x0x_ts_id[] = {
    { FT5X0X_NAME, 0 },{ }
};


MODULE_DEVICE_TABLE(i2c, ft5x0x_ts_id);

static struct i2c_driver ft5x0x_ts_driver = {
    .probe		= ft5x0x_ts_probe,
    .remove		= ft5x0x_ts_remove,
    .id_table	= ft5x0x_ts_id,
    .driver	= {
        .name	= FT5X0X_NAME,
        .owner	= THIS_MODULE,
    },
};

/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static int ft5x0x_ts_init(void)
{
    int ret;
    printk("==ft5x0x_ts_init==\n");
    ret = i2c_add_driver(&ft5x0x_ts_driver);
    printk("ret=%d\n",ret);
    return ret;
    //	return i2c_add_driver(&ft5x0x_ts_driver);
}

/***********************************************************************************************
Name	:	 

Input	:	


Output	:	

function	:	

 ***********************************************************************************************/
static void ft5x0x_ts_exit(void)
{
    printk("==ft5x0x_ts_exit==\n");
    i2c_del_driver(&ft5x0x_ts_driver);
}

module_init(ft5x0x_ts_init);
module_exit(ft5x0x_ts_exit);

MODULE_AUTHOR("<wenfs@Focaltech-systems.com>");
MODULE_DESCRIPTION("FocalTech ft5x0x TouchScreen driver");
MODULE_LICENSE("GPL");

