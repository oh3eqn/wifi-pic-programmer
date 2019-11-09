#ifndef _PARTITION_CONFIG_H__
#define _PARTITION_CONFIG_H__

#include <user_interface.h>


#define _4KB 0x1000

#if ((SPI_FLASH_SIZE_MAP == 0) || (SPI_FLASH_SIZE_MAP == 1))
#error "The flash map is not supported"
#elif (SPI_FLASH_SIZE_MAP == 2)
#define SYSTEM_PARTITION_OTA_SIZE							0x6A000
#define SYSTEM_PARTITION_OTA2_ADDR							0x81000
#define SYSTEM_PARTITION_RF_CAL_ADDR						0xFB000
#define SYSTEM_PARTITION_PHY_DATA_ADDR						0xFC000
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR				0xFD000
#define SYSTEM_PARTITION_CUSTOMER_PRIV_PARAM_ADDR           0x7C000

#elif (SPI_FLASH_SIZE_MAP == 3)
#define SYSTEM_PARTITION_OTA_SIZE							0x6A000
#define SYSTEM_PARTITION_OTA2_ADDR							0x81000
#define SYSTEM_PARTITION_RF_CAL_ADDR						0x1FB000
#define SYSTEM_PARTITION_PHY_DATA_ADDR						0x1FC000
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR				0x1FD000
#define SYSTEM_PARTITION_CUSTOMER_PRIV_PARAM_ADDR           0x7C000

#elif (SPI_FLASH_SIZE_MAP == 4)
#define SYSTEM_PARTITION_OTA_SIZE							0x6A000
#define SYSTEM_PARTITION_OTA2_ADDR							0x81000
#define SYSTEM_PARTITION_RF_CAL_ADDR						0x3FB000
#define SYSTEM_PARTITION_PHY_DATA_ADDR						0x3FC000
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR				0x3FD000
#define SYSTEM_PARTITION_CUSTOMER_PRIV_PARAM_ADDR           0x7C000

#elif (SPI_FLASH_SIZE_MAP == 5)
#define SYSTEM_PARTITION_OTA_SIZE							0x6A000
#define SYSTEM_PARTITION_OTA2_ADDR							0x101000
#define SYSTEM_PARTITION_RF_CAL_ADDR						0x1FB000
#define SYSTEM_PARTITION_PHY_DATA_ADDR						0x1FC000
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR				0x1FD000
#define SYSTEM_PARTITION_CUSTOMER_PRIV_PARAM_ADDR           0xFC000

#elif (SPI_FLASH_SIZE_MAP == 6)
#define SYSTEM_PARTITION_OTA_SIZE							0x6A000
#define SYSTEM_PARTITION_OTA2_ADDR							0x101000
#define SYSTEM_PARTITION_RF_CAL_ADDR						0x3FB000
#define SYSTEM_PARTITION_PHY_DATA_ADDR						0x3FC000
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR				0x3FD000
#define SYSTEM_PARTITION_CUSTOMER_PRIV_PARAM_ADDR           0xFC000

#else
#error "The flash map is not supported"
#endif


#define FOTA_PARTITION_OTA2_ADDR	SYSTEM_PARTITION_OTA2_ADDR


static const partition_item_t at_partition_table[] = {
    { SYSTEM_PARTITION_BOOTLOADER, 	0x0, _4KB},
    { SYSTEM_PARTITION_OTA_1, 0x1000, SYSTEM_PARTITION_OTA_SIZE},
    { SYSTEM_PARTITION_OTA_2, SYSTEM_PARTITION_OTA2_ADDR, SYSTEM_PARTITION_OTA_SIZE},
    { SYSTEM_PARTITION_RF_CAL, SYSTEM_PARTITION_RF_CAL_ADDR, _4KB},
    { SYSTEM_PARTITION_PHY_DATA, SYSTEM_PARTITION_PHY_DATA_ADDR, _4KB},
    { SYSTEM_PARTITION_SYSTEM_PARAMETER, SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR, _4KB*3},
};


#endif

