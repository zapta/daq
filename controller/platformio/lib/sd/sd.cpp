#include "sd.h"
#include "sdmmc.h"
#include "fatfs.h"
#include <cstring>

// extern SD_HandleTypeDef hsd1;

namespace sd {

void test_setup() {

FRESULT res; /* FatFs function common result code */
uint32_t byteswritten; /* File write counts */
uint8_t wtext[] = "STM32 FATFS works great!"; /* File write buffer */
uint8_t rtext[_MAX_SS];/* File read buffer */

	if(f_mount(&SDFatFS, (TCHAR const*)SDPath, 0) != FR_OK)
	{
		Error_Handler();
	}
	else
	{
		if(f_mkfs((TCHAR const*)SDPath, FM_ANY, 0, rtext, sizeof(rtext)) != FR_OK)
	    {
			Error_Handler();
	    }
		else
		{
			//Open file for writing (Create)
			if(f_open(&SDFile, "STM32.TXT", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
			{
				Error_Handler();
			}
			else
			{

				//Write to the text file
				// res = f_write(&SDFile, wtext, strlen((char *)wtext), (void *)&byteswritten);
				res = f_write(&SDFile, wtext, strlen((char *)wtext), (UINT *)&byteswritten);
				if((byteswritten == 0) || (res != FR_OK))
				{
					Error_Handler();
				}
				else
				{

					f_close(&SDFile);
				}
			}
		}
	}
	f_mount(&SDFatFS, (TCHAR const*)NULL, 0);
/* USER CODE END 2 */
  

}
void test_loop() {}



} // namespace sd