################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/adc_hal_xmc7200.c \
../mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/adc_xmc7200.c 

OBJS += \
./mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/adc_hal_xmc7200.o \
./mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/adc_xmc7200.o 

C_DEPS += \
./mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/adc_hal_xmc7200.d \
./mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/adc_xmc7200.d 


# Each subdirectory must supply rules for building sources it contributes
mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/%.o mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/%.su mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/%.cyclo: ../mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/%.c mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -D_STM32CUBE_DISCOVERY_N657_ -DLL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6 -DLL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL -DLL_ATON_RT_MODE=LL_ATON_RT_POLLING -DLL_ATON_SW_FALLBACK -DLL_ATON_DBG_BUFFER_INFO_EXCLUDED=1 -c -I../Core/Inc -I../../Secure_nsclib -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Include -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/config" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/include" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/mtkernel/kernel/knlinc" -I../Application/npu/st/ll_aton -I../Application/npu/st/device -I../Application/npu/st/inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-mtk3_bsp2-2f-sysdepend-2f-xmc_mtb-2f-device-2f-hal_adc-2f-sysdepend-2f-xmc7200

clean-mtk3_bsp2-2f-sysdepend-2f-xmc_mtb-2f-device-2f-hal_adc-2f-sysdepend-2f-xmc7200:
	-$(RM) ./mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/adc_hal_xmc7200.cyclo ./mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/adc_hal_xmc7200.d ./mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/adc_hal_xmc7200.o ./mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/adc_hal_xmc7200.su ./mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/adc_xmc7200.cyclo ./mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/adc_xmc7200.d ./mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/adc_xmc7200.o ./mtk3_bsp2/sysdepend/xmc_mtb/device/hal_adc/sysdepend/xmc7200/adc_xmc7200.su

.PHONY: clean-mtk3_bsp2-2f-sysdepend-2f-xmc_mtb-2f-device-2f-hal_adc-2f-sysdepend-2f-xmc7200

