#!/bin/sh

rm -f test
rm -f memcacheclient.o
rm -f libmemcacheclient.so.1.0.0
rm -f libmemcacheclient.so
gcc -c -g -fPIC -I./ memcacheclient.c
gcc -shared -g -Wl,-soname,libmemcacheclient.so -o libmemcacheclient.so.1.0.0 memcacheclient.o
ln -s libmemcacheclient.so.1.0.0 libmemcacheclient.so
gcc -g test.c -I./ -L./ -lmemcacheclient -o test
