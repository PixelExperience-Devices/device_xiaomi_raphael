#!/vendor/bin/sh

########################################################
### init.insmod.cfg format:                          ###
### -----------------------------------------------  ###
### [setprop|enable] [path|prop name] ###
### ...                                              ###
########################################################

if [ $# -eq 1 ]; then
  cfg_file=$1
else
  exit 1
fi

if [ -f $cfg_file ]; then
  while IFS="|" read -r action arg
  do
    case $action in
      "setprop") setprop $arg 1 ;;
      "enable") echo 1 > $arg ;;
    esac
  done < $cfg_file
fi

# Set property even if there is no insmod config
# as property value "1" is expected in early-boot trigger
setprop vendor.all.devices.ready 1
