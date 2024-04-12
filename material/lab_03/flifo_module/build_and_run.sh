#!/bin/bash

make
rmmod flifo 
insmod flifo.ko && chmod 666 /dev/flifo