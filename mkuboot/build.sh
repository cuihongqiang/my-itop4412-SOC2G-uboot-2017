#!/bin/bash

cd ../

if [ ! -f .config ]
then
	make itop4412_defconfig
fi

make -j$(nproc) ARCH=arm CROSS_COMPILE=/usr/local/arm/prebuilts-gcc-linux-x86-arm-gcc-linaro-6.3.1-2017.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-

cp u-boot.bin ./mkuboot/
echo "copy u-boot.bin done."

cd spl/
if [ ! -f itop4412-spl.bin ] ; then
	echo "notice: not found itop4412-spl.bin !"
	exit 0
else
	echo "copying itop4412-spl.bin..."
fi

cp itop4412-spl.bin ../mkuboot/
echo "copy u-boot-spl.bin done."

echo "build success !!!"
