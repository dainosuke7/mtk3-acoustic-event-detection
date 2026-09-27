################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Application/npu/st/ll_aton/ecloader.c \
../Application/npu/st/ll_aton/ll_aton.c \
../Application/npu/st/ll_aton/ll_aton_cipher.c \
../Application/npu/st/ll_aton/ll_aton_dbgtrc.c \
../Application/npu/st/ll_aton/ll_aton_debug.c \
../Application/npu/st/ll_aton/ll_aton_lib.c \
../Application/npu/st/ll_aton/ll_aton_lib_sw_operators.c \
../Application/npu/st/ll_aton/ll_aton_profiler.c \
../Application/npu/st/ll_aton/ll_aton_reloc_callbacks.c \
../Application/npu/st/ll_aton/ll_aton_reloc_network.c \
../Application/npu/st/ll_aton/ll_aton_rt_main.c \
../Application/npu/st/ll_aton/ll_aton_runtime.c \
../Application/npu/st/ll_aton/ll_aton_stai_internal.c \
../Application/npu/st/ll_aton/ll_aton_util.c \
../Application/npu/st/ll_aton/ll_sw_float.c \
../Application/npu/st/ll_aton/ll_sw_integer.c 

OBJS += \
./Application/npu/st/ll_aton/ecloader.o \
./Application/npu/st/ll_aton/ll_aton.o \
./Application/npu/st/ll_aton/ll_aton_cipher.o \
./Application/npu/st/ll_aton/ll_aton_dbgtrc.o \
./Application/npu/st/ll_aton/ll_aton_debug.o \
./Application/npu/st/ll_aton/ll_aton_lib.o \
./Application/npu/st/ll_aton/ll_aton_lib_sw_operators.o \
./Application/npu/st/ll_aton/ll_aton_profiler.o \
./Application/npu/st/ll_aton/ll_aton_reloc_callbacks.o \
./Application/npu/st/ll_aton/ll_aton_reloc_network.o \
./Application/npu/st/ll_aton/ll_aton_rt_main.o \
./Application/npu/st/ll_aton/ll_aton_runtime.o \
./Application/npu/st/ll_aton/ll_aton_stai_internal.o \
./Application/npu/st/ll_aton/ll_aton_util.o \
./Application/npu/st/ll_aton/ll_sw_float.o \
./Application/npu/st/ll_aton/ll_sw_integer.o 

C_DEPS += \
./Application/npu/st/ll_aton/ecloader.d \
./Application/npu/st/ll_aton/ll_aton.d \
./Application/npu/st/ll_aton/ll_aton_cipher.d \
./Application/npu/st/ll_aton/ll_aton_dbgtrc.d \
./Application/npu/st/ll_aton/ll_aton_debug.d \
./Application/npu/st/ll_aton/ll_aton_lib.d \
./Application/npu/st/ll_aton/ll_aton_lib_sw_operators.d \
./Application/npu/st/ll_aton/ll_aton_profiler.d \
./Application/npu/st/ll_aton/ll_aton_reloc_callbacks.d \
./Application/npu/st/ll_aton/ll_aton_reloc_network.d \
./Application/npu/st/ll_aton/ll_aton_rt_main.d \
./Application/npu/st/ll_aton/ll_aton_runtime.d \
./Application/npu/st/ll_aton/ll_aton_stai_internal.d \
./Application/npu/st/ll_aton/ll_aton_util.d \
./Application/npu/st/ll_aton/ll_sw_float.d \
./Application/npu/st/ll_aton/ll_sw_integer.d 


# Each subdirectory must supply rules for building sources it contributes
Application/npu/st/ll_aton/%.o Application/npu/st/ll_aton/%.su Application/npu/st/ll_aton/%.cyclo: ../Application/npu/st/ll_aton/%.c Application/npu/st/ll_aton/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -D_STM32CUBE_DISCOVERY_N657_ -DLL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6 -DLL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL -DLL_ATON_RT_MODE=LL_ATON_RT_POLLING -DLL_ATON_SW_FALLBACK -DLL_ATON_DBG_BUFFER_INFO_EXCLUDED=1 -c -I../Core/Inc -I../../Secure_nsclib -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Include -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/config" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/include" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/mtkernel/kernel/knlinc" -I../Application/npu/st/ll_aton -I../Application/npu/st/device -I../Application/npu/st/inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Application-2f-npu-2f-st-2f-ll_aton

clean-Application-2f-npu-2f-st-2f-ll_aton:
	-$(RM) ./Application/npu/st/ll_aton/ecloader.cyclo ./Application/npu/st/ll_aton/ecloader.d ./Application/npu/st/ll_aton/ecloader.o ./Application/npu/st/ll_aton/ecloader.su ./Application/npu/st/ll_aton/ll_aton.cyclo ./Application/npu/st/ll_aton/ll_aton.d ./Application/npu/st/ll_aton/ll_aton.o ./Application/npu/st/ll_aton/ll_aton.su ./Application/npu/st/ll_aton/ll_aton_cipher.cyclo ./Application/npu/st/ll_aton/ll_aton_cipher.d ./Application/npu/st/ll_aton/ll_aton_cipher.o ./Application/npu/st/ll_aton/ll_aton_cipher.su ./Application/npu/st/ll_aton/ll_aton_dbgtrc.cyclo ./Application/npu/st/ll_aton/ll_aton_dbgtrc.d ./Application/npu/st/ll_aton/ll_aton_dbgtrc.o ./Application/npu/st/ll_aton/ll_aton_dbgtrc.su ./Application/npu/st/ll_aton/ll_aton_debug.cyclo ./Application/npu/st/ll_aton/ll_aton_debug.d ./Application/npu/st/ll_aton/ll_aton_debug.o ./Application/npu/st/ll_aton/ll_aton_debug.su ./Application/npu/st/ll_aton/ll_aton_lib.cyclo ./Application/npu/st/ll_aton/ll_aton_lib.d ./Application/npu/st/ll_aton/ll_aton_lib.o ./Application/npu/st/ll_aton/ll_aton_lib.su ./Application/npu/st/ll_aton/ll_aton_lib_sw_operators.cyclo ./Application/npu/st/ll_aton/ll_aton_lib_sw_operators.d ./Application/npu/st/ll_aton/ll_aton_lib_sw_operators.o ./Application/npu/st/ll_aton/ll_aton_lib_sw_operators.su ./Application/npu/st/ll_aton/ll_aton_profiler.cyclo ./Application/npu/st/ll_aton/ll_aton_profiler.d ./Application/npu/st/ll_aton/ll_aton_profiler.o ./Application/npu/st/ll_aton/ll_aton_profiler.su ./Application/npu/st/ll_aton/ll_aton_reloc_callbacks.cyclo ./Application/npu/st/ll_aton/ll_aton_reloc_callbacks.d ./Application/npu/st/ll_aton/ll_aton_reloc_callbacks.o ./Application/npu/st/ll_aton/ll_aton_reloc_callbacks.su ./Application/npu/st/ll_aton/ll_aton_reloc_network.cyclo ./Application/npu/st/ll_aton/ll_aton_reloc_network.d ./Application/npu/st/ll_aton/ll_aton_reloc_network.o ./Application/npu/st/ll_aton/ll_aton_reloc_network.su ./Application/npu/st/ll_aton/ll_aton_rt_main.cyclo ./Application/npu/st/ll_aton/ll_aton_rt_main.d ./Application/npu/st/ll_aton/ll_aton_rt_main.o ./Application/npu/st/ll_aton/ll_aton_rt_main.su ./Application/npu/st/ll_aton/ll_aton_runtime.cyclo ./Application/npu/st/ll_aton/ll_aton_runtime.d ./Application/npu/st/ll_aton/ll_aton_runtime.o ./Application/npu/st/ll_aton/ll_aton_runtime.su ./Application/npu/st/ll_aton/ll_aton_stai_internal.cyclo ./Application/npu/st/ll_aton/ll_aton_stai_internal.d ./Application/npu/st/ll_aton/ll_aton_stai_internal.o ./Application/npu/st/ll_aton/ll_aton_stai_internal.su ./Application/npu/st/ll_aton/ll_aton_util.cyclo ./Application/npu/st/ll_aton/ll_aton_util.d ./Application/npu/st/ll_aton/ll_aton_util.o ./Application/npu/st/ll_aton/ll_aton_util.su ./Application/npu/st/ll_aton/ll_sw_float.cyclo ./Application/npu/st/ll_aton/ll_sw_float.d ./Application/npu/st/ll_aton/ll_sw_float.o ./Application/npu/st/ll_aton/ll_sw_float.su ./Application/npu/st/ll_aton/ll_sw_integer.cyclo ./Application/npu/st/ll_aton/ll_sw_integer.d ./Application/npu/st/ll_aton/ll_sw_integer.o ./Application/npu/st/ll_aton/ll_sw_integer.su

.PHONY: clean-Application-2f-npu-2f-st-2f-ll_aton

