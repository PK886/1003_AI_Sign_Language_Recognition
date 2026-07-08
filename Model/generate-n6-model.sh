#!/bin/bash

stedgeai generate --no-inputs-allocation --name palm_detection --model 033_palm_detection_full_quant_pc_uf_od.tflite --target stm32n6 --st-neural-art palm_detection@user_neuralart.json
cp st_ai_output/palm_detection_ecblobs.h .
cp st_ai_output/palm_detection.c .
cp st_ai_output/palm_detection_atonbuf.xSPI2.raw palm_detection_data.xSPI2.bin
arm-none-eabi-objcopy -I binary palm_detection_data.xSPI2.bin --change-addresses 0x70200000 -O ihex palm-detection-data.hex

stedgeai generate --name hand_landmarks --model hand_landmarks_full_224_int8_pc.tflite --target stm32n6 --st-neural-art hand_landmarks@user_neuralart.json
cp st_ai_output/hand_landmarks_ecblobs.h .
cp st_ai_output/hand_landmarks.c .
cp st_ai_output/hand_landmarks_atonbuf.xSPI2.raw hand_landmarks_data.xSPI2.bin
arm-none-eabi-objcopy -I binary hand_landmarks_data.xSPI2.bin --change-addresses 0x70400000 -O ihex hand-landmarks-data.hex

sed '$d' palm-detection-data.hex > temp.hex
cat temp.hex hand-landmarks-data.hex > ../Binary/network-data.hex