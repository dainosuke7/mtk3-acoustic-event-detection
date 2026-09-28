################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/cache.c \
../mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/cpu_cntl.c \
../mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/exc_hdr.c \
../mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/interrupt.c \
../mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/sys_start.c 

S_UPPER_SRCS += \
../mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/dispatch.S 

OBJS += \
./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/cache.o \
./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/cpu_cntl.o \
./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/dispatch.o \
./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/exc_hdr.o \
./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/interrupt.o \
./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/sys_start.o 

S_UPPER_DEPS += \
./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/dispatch.d 

C_DEPS += \
./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/cache.d \
./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/cpu_cntl.d \
./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/exc_hdr.d \
./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/interrupt.d \
./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/sys_start.d 


# Each subdirectory must supply rules for building sources it contributes
mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/%.o mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/%.su mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/%.cyclo: ../mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/%.c mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -D_STM32CUBE_DISCOVERY_N657_ -DLL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6 -DLL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL -DLL_ATON_RT_MODE=LL_ATON_RT_POLLING -DLL_ATON_SW_FALLBACK -DLL_ATON_DBG_BUFFER_INFO_EXCLUDED=1 -c -I../Core/Inc -I../../Secure_nsclib -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Include -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/config" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/include" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/mtkernel/kernel/knlinc" -I../Application/npu/st/ll_aton -I../Application/npu/st/device -I../Application/npu/st/inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/%.o: ../mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/%.S mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m55 -g3 -DDEBUG -D_STM32CUBE_DISCOVERY_N657_ -c -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/config" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/include" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/mtkernel/kernel/knlinc" -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-mtk3_bsp2-2f-sysdepend-2f-ra_fsp-2f-cpu-2f-core-2f-armv8m

clean-mtk3_bsp2-2f-sysdepend-2f-ra_fsp-2f-cpu-2f-core-2f-armv8m:
	-$(RM) ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/cache.cyclo ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/cache.d ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/cache.o ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/cache.su ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/cpu_cntl.cyclo ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/cpu_cntl.d ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/cpu_cntl.o ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/cpu_cntl.su ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/dispatch.d ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/dispatch.o ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/exc_hdr.cyclo ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/exc_hdr.d ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/exc_hdr.o ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/exc_hdr.su ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/interrupt.cyclo ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/interrupt.d ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/interrupt.o ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/interrupt.su ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/sys_start.cyclo ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/sys_start.d ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/sys_start.o ./mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/sys_start.su

.PHONY: clean-mtk3_bsp2-2f-sysdepend-2f-ra_fsp-2f-cpu-2f-core-2f-armv8m

