################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Application/npu/aed_model_network.c \
../Application/npu/aed_model_stai.c \
../Application/npu/infer_task.c \
../Application/npu/npu_cache_port.c \
../Application/npu/npu_hw.c \
../Application/npu/npu_rt.c \
../Application/npu/npu_selftest.c 

OBJS += \
./Application/npu/aed_model_network.o \
./Application/npu/aed_model_stai.o \
./Application/npu/infer_task.o \
./Application/npu/npu_cache_port.o \
./Application/npu/npu_hw.o \
./Application/npu/npu_rt.o \
./Application/npu/npu_selftest.o 

C_DEPS += \
./Application/npu/aed_model_network.d \
./Application/npu/aed_model_stai.d \
./Application/npu/infer_task.d \
./Application/npu/npu_cache_port.d \
./Application/npu/npu_hw.d \
./Application/npu/npu_rt.d \
./Application/npu/npu_selftest.d 


# Each subdirectory must supply rules for building sources it contributes
Application/npu/%.o Application/npu/%.su Application/npu/%.cyclo: ../Application/npu/%.c Application/npu/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -D_STM32CUBE_DISCOVERY_N657_ -DLL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6 -DLL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL -DLL_ATON_RT_MODE=LL_ATON_RT_POLLING -DLL_ATON_SW_FALLBACK -DLL_ATON_DBG_BUFFER_INFO_EXCLUDED=1 -c -I../Core/Inc -I../../Secure_nsclib -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Include -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/config" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/include" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/mtkernel/kernel/knlinc" -I../Application/npu/st/ll_aton -I../Application/npu/st/device -I../Application/npu/st/inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Application-2f-npu

clean-Application-2f-npu:
	-$(RM) ./Application/npu/aed_model_network.cyclo ./Application/npu/aed_model_network.d ./Application/npu/aed_model_network.o ./Application/npu/aed_model_network.su ./Application/npu/aed_model_stai.cyclo ./Application/npu/aed_model_stai.d ./Application/npu/aed_model_stai.o ./Application/npu/aed_model_stai.su ./Application/npu/infer_task.cyclo ./Application/npu/infer_task.d ./Application/npu/infer_task.o ./Application/npu/infer_task.su ./Application/npu/npu_cache_port.cyclo ./Application/npu/npu_cache_port.d ./Application/npu/npu_cache_port.o ./Application/npu/npu_cache_port.su ./Application/npu/npu_hw.cyclo ./Application/npu/npu_hw.d ./Application/npu/npu_hw.o ./Application/npu/npu_hw.su ./Application/npu/npu_rt.cyclo ./Application/npu/npu_rt.d ./Application/npu/npu_rt.o ./Application/npu/npu_rt.su ./Application/npu/npu_selftest.cyclo ./Application/npu/npu_selftest.d ./Application/npu/npu_selftest.o ./Application/npu/npu_selftest.su

.PHONY: clean-Application-2f-npu

