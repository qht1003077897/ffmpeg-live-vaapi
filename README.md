  FFmpeg/vaapi Sample Player (ffvademo)

  Copyright (C) 2014 Intel Corporation
    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>


License
-------

The FFmpeg/vaapi Sample Player (ffvademo) and its associated helper
library are available available under the terms of the GNU Lesser
General Public License v2.1+


Overview
--------

This project consists in a simple FFmpeg/vaapi based video player, and
an associated helper library, that demonstrate how to use FFmpeg with
Video Acceleration (VA) APIs.


Features
--------

  * FFmpeg decoder with VA-API support
  * Native X11 renderer through vaPutSurface()
  * EGL renderer showing off VA/EGL interop capabilities

Non-goals

  * A full-blown player supporting every possible codec
  * A player that supports audio decoding
  * A player that supports software decoding


Requirements
------------

Software requirements

  * VA-API (>= 0.36.0 for VA/EGL interop)
  * FFmpeg (>= 2.1)


Usage
-----

  * Play an H.264 video with an MP4 container
    $ ffvademo /path/to/video.mp4

  * Play a VC-1 video while converting the output to I420 format
    $ ffvademo -f yuv420p /path/to/video.wmv

  * Play an H.264 video while rendering with EGL/GLESv2
    $ ffvademo -r egl -f argb /path/to/video.mp4

## 编译说明：

1. sudo apt install libva-dev libegl-dev，安装编译依赖。
2. 使用cmake . 在本地生成Makefile
3. make编译，得到ffvademo可执行程序。
4. 执行./ffvademo a.mp4 -r egl，使用egl渲染视频。
5. 假如没有找到 vaapi 后端库，试试 export LIBVA_DRIVER_NAME=jmgpu，然后再运行ffvademo程序。

## 程序数据流

![image-20230616142827617](C:\Users\dengzhihong876\AppData\Roaming\Typora\typora-user-images\image-20230616142827617.png)

1. ffmpeg调用vaapi解码，得到一个AVFrame
2. 取AVFrame的data[3]，得到一个VASurfaceID
3. 通过vaDeriveImage将VASurfaceID转换成一个VAImage
4. 通过avAcquireBufferHandle将AVImage的信息提取出来，得到VABufferInfo
5. 通过eglCreateImageKHR，将VABufferInfo的内容填充到attributes中，根据这个信息创建衣蛾EGLImageKHR
6. 使用eglImageTargetTexture2DOES，将EGLImageKHR附加到opengl的texture上。
7. 之后就是正常的opengl纹理操作。

以上就是vaapi解码的数据，是如何与opengl结合，显示在界面上的。其他的细节这里忽略。在贴纹理显示视频之前，需要调用 vaSyncSurface，来保证解码已经处理完，否则可能会出现绿色的墙砖一样的问题。