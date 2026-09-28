################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/cpu_cntl.c \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/exc_hdr.c \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/interrupt.c \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/reset_main.c \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/vector_tbl.c 

S_UPPER_SRCS += \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/dispatch.S \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/int_asm.S \
../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/reset_hdl.S 

OBJS += \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/cpu_cntl.o \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/dispatch.o \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/exc_hdr.o \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/int_asm.o \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/interrupt.o \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/reset_hdl.o \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/reset_main.o \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/vector_tbl.o 

S_UPPER_DEPS += \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/dispatch.d \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/int_asm.d \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/reset_hdl.d 

C_DEPS += \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/cpu_cntl.d \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/exc_hdr.d \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/interrupt.d \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/reset_main.d \
./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/vector_tbl.d 


# Each subdirectory must supply rules for building sources it contributes
mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/%.o mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/%.su mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/%.cyclo: ../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/%.c mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -D_STM32CUBE_DISCOVERY_N657_ -DLL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6 -DLL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL -DLL_ATON_RT_MODE=LL_ATON_RT_POLLING -DLL_ATON_SW_FALLBACK -DLL_ATON_DBG_BUFFER_INFO_EXCLUDED=1 -c -I../Core/Inc -I../../Secure_nsclib -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Include -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/config" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/include" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/mtkernel/kernel/knlinc" -I../Application/npu/st/ll_aton -I../Application/npu/st/device -I../Application/npu/st/inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/%.o: ../mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/%.S mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m55 -g3 -DDEBUG -D_STM32CUBE_DISCOVERY_N657_ -c -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/config" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/include" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/mtkernel/kernel/knlinc" -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-mtk3_bsp2-2f-mtkernel-2f-kernel-2f-sysdepend-2f-cpu-2f-core-2f-rxv2

clean-mtk3_bsp2-2f-mtkernel-2f-kernel-2f-sysdepend-2f-cpu-2f-core-2f-rxv2:
	-$(RM) ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/cpu_cntl.cyclo ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/cpu_cntl.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/cpu_cntl.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/cpu_cntl.su ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/dispatch.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/dispatch.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/exc_hdr.cyclo ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/exc_hdr.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/exc_hdr.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/exc_hdr.su ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/int_asm.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/int_asm.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/interrupt.cyclo ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/interrupt.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/interrupt.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/interrupt.su ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/reset_hdl.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/reset_hdl.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/reset_main.cyclo ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/reset_main.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/reset_main.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/reset_main.su ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/vector_tbl.cyclo ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/vector_tbl.d ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/vector_tbl.o ./mtk3_bsp2/mtkernel/kernel/sysdepend/cpu/core/rxv2/vector_tbl.su

.PHONY: clean-mtk3_bsp2-2f-mtkernel-2f-kernel-2f-sysdepend-2f-cpu-2f-core-2f-rxv2

