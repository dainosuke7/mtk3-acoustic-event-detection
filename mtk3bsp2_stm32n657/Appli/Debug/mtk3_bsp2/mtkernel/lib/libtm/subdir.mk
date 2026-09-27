################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../mtk3_bsp2/mtkernel/lib/libtm/libtm.c \
../mtk3_bsp2/mtkernel/lib/libtm/libtm_printf.c 

OBJS += \
./mtk3_bsp2/mtkernel/lib/libtm/libtm.o \
./mtk3_bsp2/mtkernel/lib/libtm/libtm_printf.o 

C_DEPS += \
./mtk3_bsp2/mtkernel/lib/libtm/libtm.d \
./mtk3_bsp2/mtkernel/lib/libtm/libtm_printf.d 


# Each subdirectory must supply rules for building sources it contributes
mtk3_bsp2/mtkernel/lib/libtm/%.o mtk3_bsp2/mtkernel/lib/libtm/%.su mtk3_bsp2/mtkernel/lib/libtm/%.cyclo: ../mtk3_bsp2/mtkernel/lib/libtm/%.c mtk3_bsp2/mtkernel/lib/libtm/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -D_STM32CUBE_DISCOVERY_N657_ -DLL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6 -DLL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL -DLL_ATON_RT_MODE=LL_ATON_RT_POLLING -DLL_ATON_SW_FALLBACK -DLL_ATON_DBG_BUFFER_INFO_EXCLUDED=1 -c -I../Core/Inc -I../../Secure_nsclib -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Include -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/config" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/include" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/mtkernel/kernel/knlinc" -I../Application/npu/st/ll_aton -I../Application/npu/st/device -I../Application/npu/st/inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-mtk3_bsp2-2f-mtkernel-2f-lib-2f-libtm

clean-mtk3_bsp2-2f-mtkernel-2f-lib-2f-libtm:
	-$(RM) ./mtk3_bsp2/mtkernel/lib/libtm/libtm.cyclo ./mtk3_bsp2/mtkernel/lib/libtm/libtm.d ./mtk3_bsp2/mtkernel/lib/libtm/libtm.o ./mtk3_bsp2/mtkernel/lib/libtm/libtm.su ./mtk3_bsp2/mtkernel/lib/libtm/libtm_printf.cyclo ./mtk3_bsp2/mtkernel/lib/libtm/libtm_printf.d ./mtk3_bsp2/mtkernel/lib/libtm/libtm_printf.o ./mtk3_bsp2/mtkernel/lib/libtm/libtm_printf.su

.PHONY: clean-mtk3_bsp2-2f-mtkernel-2f-lib-2f-libtm

