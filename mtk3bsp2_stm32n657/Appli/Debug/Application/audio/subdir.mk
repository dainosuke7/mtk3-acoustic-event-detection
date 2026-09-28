################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Application/audio/audio_task.c \
../Application/audio/mdf_io.c \
../Application/audio/pcm_fifo.c \
../Application/audio/sai_io.c \
../Application/audio/tap_ring.c \
../Application/audio/wm8904.c 

OBJS += \
./Application/audio/audio_task.o \
./Application/audio/mdf_io.o \
./Application/audio/pcm_fifo.o \
./Application/audio/sai_io.o \
./Application/audio/tap_ring.o \
./Application/audio/wm8904.o 

C_DEPS += \
./Application/audio/audio_task.d \
./Application/audio/mdf_io.d \
./Application/audio/pcm_fifo.d \
./Application/audio/sai_io.d \
./Application/audio/tap_ring.d \
./Application/audio/wm8904.d 


# Each subdirectory must supply rules for building sources it contributes
Application/audio/%.o Application/audio/%.su Application/audio/%.cyclo: ../Application/audio/%.c Application/audio/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -D_STM32CUBE_DISCOVERY_N657_ -DLL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6 -DLL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL -DLL_ATON_RT_MODE=LL_ATON_RT_POLLING -DLL_ATON_SW_FALLBACK -DLL_ATON_DBG_BUFFER_INFO_EXCLUDED=1 -c -I../Core/Inc -I../../Secure_nsclib -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Include -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/config" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/include" -I"C:/Users/daich/work/tron/mtk3bsp2_stm32n657/Appli/mtk3_bsp2/mtkernel/kernel/knlinc" -I../Application/npu/st/ll_aton -I../Application/npu/st/device -I../Application/npu/st/inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Application-2f-audio

clean-Application-2f-audio:
	-$(RM) ./Application/audio/audio_task.cyclo ./Application/audio/audio_task.d ./Application/audio/audio_task.o ./Application/audio/audio_task.su ./Application/audio/mdf_io.cyclo ./Application/audio/mdf_io.d ./Application/audio/mdf_io.o ./Application/audio/mdf_io.su ./Application/audio/pcm_fifo.cyclo ./Application/audio/pcm_fifo.d ./Application/audio/pcm_fifo.o ./Application/audio/pcm_fifo.su ./Application/audio/sai_io.cyclo ./Application/audio/sai_io.d ./Application/audio/sai_io.o ./Application/audio/sai_io.su ./Application/audio/tap_ring.cyclo ./Application/audio/tap_ring.d ./Application/audio/tap_ring.o ./Application/audio/tap_ring.su ./Application/audio/wm8904.cyclo ./Application/audio/wm8904.d ./Application/audio/wm8904.o ./Application/audio/wm8904.su

.PHONY: clean-Application-2f-audio

