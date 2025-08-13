#!/bin/bash
# Simple script to make codeplug changes a bit easier and faster
# OH1KH -2018
#
# change file path to place where you keep stored codeplugs and the name of your text editor !
# check that your dmrconfig is found along default search path. Otherwise set your special path
# to front of "dmrconfig" commands below (3 lines below needs that)
#
path="/home/yourusername/yourcodeplugstore/"
editor="leafpad"
backup=$(date +%Y.%m.%d_%T_device.conf)
CR="\033[1;31m"
CG="\033[1;32m"
CY="\033[1;33m"
NC="\033[0m" 
#############################################################################
cd $path
echo -e "${CG}"
echo -e "Working order of this script: ${NC}"
echo -e "${CY}- read image (device.img, device.conf) from radio: ${NC} dmrconfig -r"
echo -e "${CY}  or continue with stored conf giving it's name as 1st starting"
echo "  parameter for this script. (file device.img (old) must exist in directory)"
echo
echo "- edit device.conf. Remember to save before closing !!"
echo -e "- backup edited conf using name: ${NC} $backup"
echo -e "${CY}"
echo -e "- drop your conf to image: ${NC}dmrconfig -c  device.img  device.conf"
echo -e -n "${CY}"
echo "    At this point you will get errors if something is wrong."
echo "    Load backup, fix errors and try again."
echo "    If you give conf name as 1st parameter for script it will not"
echo "    read radio and continues from conf edit."
echo
echo -e "- load new image to radio:${NC} dmrconfig -w device.img"
echo -e "${CY}"
if [ -n "$1" ]
    then
    echo -e "${CG}"
    echo -e "Without reading radio continue with conf:\n${NC}$1"
    echo -e "${CY}"
    if [ ! -e "$1" ]
     then
	echo -e "${CR}File not found !! ${NC}"
	exit
     fi
fi    
#
echo -e " ${CG}Start now ? ${NC}(Y/n)"
echo
read answer
if [ "N" == "$answer" ] || [ "n" == "$answer" ]
then
    exit
fi
#
if [ -e "$1" ]
then
    echo -e "${CG}"
    echo -e "Continue with fixed conf: ${NC}$1"
    echo -e "${CG}"
    echo -e "Copy to name:${NC} device.conf"
    cp -f $1 device.conf
    echo -e "${CY}"
    echo "!!!!Remember to save file if you make new changes here !!!!" 
    echo "Othewise just close editor to continue..."
    echo -n -e "${NC}"
else
    echo -e "${CG}"
    echo "Read from radio..."
    echo -n -e "${NC}"
    dmrconfig -r
fi
#
echo -e "${CG}"
echo "Edit..."
echo -n -e "${NC}"
$editor device.conf
#
echo -e "${CG}"
echo "Add conf to imag..."
echo -n -e "${NC}"
dmrconfig -c device.img device.conf
if [ $? -ne 0 ]
    then
    echo -e "${CR}Errors. Fix them and try again!.${NC}"
    exit
fi
#
echo -e "${CG}"
echo "Backup..."
echo -n -e "${NC}"
cp device.conf $backup
#
echo -e "${CG}"
echo "Write image to radio..."
echo -n -e "${NC}"
dmrconfig -w device.img
#
echo -e "${CG}"
echo "All done !"
echo -n -e "${NC}"
