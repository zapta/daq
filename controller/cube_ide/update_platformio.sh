#!/bin/bash -x

dst="../platformio"

rm -r $dst/lib/Core
cp -r Core $dst/lib/Core

# Patch Core related stuff
mv  $dst/lib/Core/Src/freertos.c  $dst/lib/Core/Src/freertos.c.ignored
mv  $dst/lib/Core/Src/main.c  $dst/lib/Core/Src/main.c.ignored

rm -rf $dst/src/startup
mv $dst/lib/Core/Startup $dst/src/startup

rm -r $dst/lib/Drivers
cp -r Drivers $dst/lib/Drivers

rm -r $dst/lib/Middlewares
cp -r Middlewares $dst/lib/Middlewares

rm -r $dst/lib/USB_DEVICE
cp -r USB_DEVICE $dst/lib/USB_DEVICE

cp STM32H750VBTX_FLASH.ld  $dst/STM32H750VBTX_FLASH.ld



