## Dependencies

### gcc (ucrt64)
    pacman -S mingw-w64-ucrt-x86_64-gcc

### json-c
    pacman -S mingw-w64-ucrt-x86_64-json-c

### gtk+-3.0
    pacman -S mingw-w64-ucrt-x86_64-gtk3

### gstreamer-1.0
    pacman -S mingw-w64-ucrt-x86_64-gstreamer
    pacman -S mingw-w64-ucrt-x86_64-gst-rtsp-server
    pacman -S mingw-w64-ucrt-x86_64-gst-libav
    pacman -S mingw-w64-ucrt-x86_64-youtube-dl

### libsoup-2.4
    pacman -S mingw-w64-ucrt-x86_64-libsoup


### gnutls
    pacman -S libgnutls-devel

### tools

### build libsecp256k1
    pacman -S autoconf-wrapper
    pacman -S libtool
    pacman -S mingw-w64-ucrt-x86_64-libltdl
    pacman -S automake-wrapper

    git clone https://github.com/bitcoin-core/secp256k1.git
    cd secp256k1
    ./autogen.sh
    ./configure 
    make 
    make install
