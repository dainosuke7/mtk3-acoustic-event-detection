################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/cpu_cntl.c \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/exc_hdl.c \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/interrupt.c \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/reset_main.c 

S_UPPER_SRCS += \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/dispatch.S \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/exc_entry.S \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/int_asm.S \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/reset_hdl.S \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/vector_tbl.S 

OBJS += \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/cpu_cntl.o \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/dispatch.o \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/exc_entry.o \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/exc_hdl.o \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/int_asm.o \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/interrupt.o \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/reset_hdl.o \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/reset_main.o \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/vector_tbl.o 

S_UPPER_DEPS += \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/dispatch.d \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/exc_entry.d \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/int_asm.d \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/reset_hdl.d \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/vector_tbl.d 

C_DEPS += \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/cpu_cntl.d \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/exc_hdl.d \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/interrupt.d \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/reset_main.d 


# Each subdirectory must supply rules for building sources it contributes
mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/%.o mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/%.su mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/%.cyclo: ../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/%.c mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -D_STM32CUBE_DISCOVERY_N657_ -DLL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6 -DLL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL -DLL_ATON_RT_MODE=LL_ATON_RT_POLLING -DLL_ATON_SW_FALLBACK -DLL_ATON_DBG_BUFFER_INFO_EXCLUDED=1 -c -I../Core/Inc -I../../Secure_nsclib -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Include -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/config" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/include" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/mtkernel/kernel/knlinc" -I../Application/npu/st/ll_aton -I../Application/npu/st/device -I../Application/npu/st/inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/%.o: ../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/%.S mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m55 -g3 -DDEBUG -D_STM32CUBE_DISCOVERY_N657_ -c -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/config" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/include" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/mtkernel/kernel/knlinc" -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-mtk3_bsp2-2f-mtkernel-2f-kernel-2f-sysdepend-2f-cpu-2f-core-2f-armv7a

clean-mtk3_bsp2-2f-mtkernel-2f-kernel-2f-sysdepend-2f-cpu-2f-core-2f-armv7a:
	-$(RM) ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/cpu_cntl.cyclo ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/cpu_cntl.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/cpu_cntl.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/cpu_cntl.su ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/dispatch.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/dispatch.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/exc_entry.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/exc_entry.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/exc_hdl.cyclo ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/exc_hdl.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/exc_hdl.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/exc_hdl.su ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/int_asm.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/int_asm.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/interrupt.cyclo ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/interrupt.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/interrupt.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/interrupt.su ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/reset_hdl.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/reset_hdl.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/reset_main.cyclo ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/reset_main.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/reset_main.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/reset_main.su ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/vector_tbl.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/armv7a/vector_tbl.o

.PHONY: clean-mtk3_bsp2-2f-mtkernel-2f-kernel-2f-sysdepend-2f-cpu-2f-core-2f-armv7a

