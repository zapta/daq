#!/bin/bash -x

dst="../platformio"
dstlib="../platformio/lib/cube_ide"

rm -r $dstlib/Core
cp -r Core $dstlib/Core

# Patch Core related stuff
mv  $dstlib/Core/Src/freertos.c  $dstlib/Core/Src/freertos.c.ignored
mv  $dstlib/Core/Src/main.c  $dstlib/Core/Src/main.c.ignored

rm -rf $dst/src/startup
mv $dstlib/Core/Startup $dst/src/startup

rm -r $dstlib/Drivers
cp -r Drivers $dstlib/Drivers

rm -r $dstlib/Middlewares
cp -r Middlewares $dstlib/Middlewares

rm -r $dstlib/USB_DEVICE
cp -r USB_DEVICE $dstlib/USB_DEVICE

cp STM32H750VBTX_FLASH.ld  $dst/STM32H750VBTX_FLASH.ld

cp cube_ide.pdf ../docs/cube_ide_report.pdf




