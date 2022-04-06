libjpeg-turbo-vc8000
----
VC8000 supported hardward H264 and JPEG decoder for MA35D1. [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) is a JPEG image codec that uses SIMD instruction to accelerate baseline JPEG compression and decompression. The goal of this repository is to integrate the hardware JPEG decoder of VC8000 into libjpeg-turbo.  
VC8000 JPEG decoder support  
* Maximum output resolution: 1920 x 1080  
* Color space: ARGB, BGRA, RGB, BGR  
* Direct output to ultrafb(/dev/fb0)
## Requirement  
1. MA35D1 SDK package which exported form MA35D1 Yocto project.
2. libjpeg-turbo v2.1.3
3. pathelf tool  
    #sudo apt install patchelf 
## Build  
1. Keep internet connection, it will download libjpeg-turbo from offical websit.  
2. Execute build script  
    #./build_aarch64.sh
3. Pack libjpeg-turbo-vc8000 install package(libjpeg-trubo_target.tar.gz)  
    #./pack.sh
## Install
1. Install libjpeg-turbo-vc8000 library to MA35D1 target board  
    a. Copy libjpeg-turbo_target.tar.gz and target_libjpeg-turbo_install.sh to target board  
    b. Run install script on target  
    &emsp;#./target_libjpeg-turbo_install.sh  
2. Replace ma35d1-vc8000 kernel module  
    a. Copy prebuilt "module/ma35d1-vc8000.ko" kernel module to target "/lib/modules/5.4.110/" folder
## Performance
Memory buffer output: Tested by tjbench
Source Image Resolution | Scaled Image Resolution | w/ VC8000 (fps)  | w/o VC8000 (fps)  | compare (%)
:-----------------------|-------------------------|------------------|-------------------|---------------
227x149                 |227x149                  |186.9             |482.8              | -61.2
227x149                 |426x280                  |78.5              |124.6              | -36.9
227x149                 |114x75                   |259.8             |758.5              | -65.7
640x400                 |640x400                  |39.0              |47.6               | -18.0
640x400                 |1200x750                 |17.3              |15.8               | +9.4
640x480                 |320x200                  |75.0              |69.6               | +7.7
1608x1072               |1608x1072                |11.75             |7.7                | +52.5
1608x1072               |804x536                  |19.8              |10.6               | +86.7

Ultrafb output: Tested by FBDirectOut
Source Image Resolution | Scaled Image Resolution | fps
:-----------------------|-------------------------|-------------
227x149                 |227x149                  |416.6
227x149                 |512x300                  |312.5
640x400                 |640x400                  |188.6
640x400                 |512x300                  |181.8
640x400                 |1024x600                 |123.4
1608x1072               |512x300                  |38.0
1608x1072               |1024x600                 |38.1








