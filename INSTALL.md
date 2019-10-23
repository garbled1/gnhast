# Just some hints on building for now #

## Debian Linux (10) ##

### Install the following: ###

```
apt install libevent-2.1.6 libevent-extra-2.1-6 libevent-openssl-2.1-6 libevent-pthreads-2.1-6 build-essential autoconf automake libtool flex bison gdb libevent-dev libssl-dev librrd-dev rrdtool pkg-config libxml2-dev libbsd-dev
```

### Run: ###

```
./autoconf
./configure
make
sudo make install
sudo ldconfig
```

