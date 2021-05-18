Video Processor
===============

***Video Processor: High-end video processing on live data for the rest of us***

Video Processor is a Windows application which couples a capture card to a high quality video renderer ([madVR](http://madvr.com/)) and takes care of all the plumbing in such a way the metadata stays intact. This allows the renderer to do things like 3d LUT, HDR tone mapping, scaling, deinterlacing and much more which can significantly improve image quality on the majority of displays. It is especially useful for accurate color-correction and HDR-like display on low lumen devices like projectors.

**Background**

The madVR renderer is legendary for it's video processing prowess, but its not a complete solution - it is only  a DirectShow renderer not a full player. A variety of players and sources are available, but as it stands only file-based sources (ripped Blue-rays etc) have the HDR metadata which allows madVR to determine what it is actually displaying. To date, none of the capture card vendors have implemented the required metadata in their sources and hence none of the players works correctly out of the box as the data simply is not there.

A solution for this problem already exists in the form of the [directshow_metadata_injector_filter](https://github.com/defl/directshow_metadata_injector_filter), which is a DirectShow filter that can inject the correct metadata between the source and the renderer. It is significantly less user-friendly than this application as it requires manual configuration or external hardware and scripting. 

VideoProcessor is a one-click solution for this problem allowing live streaming, processing and glorious rendering of anything from low-end PAL to high end 4k HDR sources.

**HDCP**

There is one snag through, all high quality copyright protected video is protected with High-bandwidth Digital Content Protection (HDCP). No retail capture card is allowed to forward unprotected video data to it's clients. Therefore, if you connect a HDCP source to a capture card the video output will be disabled or blank. VideoProcessor is a just a client of your capture card and hence if your capture card does not output video because of HDCP, there is no video to process. VideoProcessor cannot strip, circumvent or work around HDCP in any way, shape or form, it can only process what it is given.

Devices which can remove this protection are available, allowing for HDCP protected sources being captured by your capture card, but their legality depends on your jurisdiction and use. Ensuring compliance with your local laws, and feeding your capture card data it can forward to VideoProcessor, is <u>your</u> responsibility. 

**Showtime!**

With all that out of the way, get the latest release at http://www.videoprocessor.org/ or the code at https://github.com/defl/videoprocessor.



# Installing it

- Install VS2019 x64 runtime
- Install capture card (see below) and drivers, verify the card and capture works by running the vendor's capture application.
- Download VideoProcessor.exe 
- Configure madVR
  - TODO: Insert links to guides
- Enjoy
- (After this there will be lots of madVR fiddling and probably ordering a bigger GPU because you've figured out that the highest settings are just a bit better. Welcome to the hobby.)



# Capture cards

VideoProcessor has a wide range of applications. You can do as little as just some color correction on low end inputs like NTSC all the way up to HDR tone mapping a 4k HDR source. As such there is a very wide variety of cards it can potentially work with. The following list provides cards which provide HDMI 2.0 or up, 4K or up, HDR or better and >=10bits - which covers the most common use case of processing a HDR stream and polishing + tone mapping it to your display.

**Tested and confirmed working**

 * [BlackMagic DeckLink Mini Recorder 4k](https://www.blackmagicdesign.com/nl/products/decklink/techspecs/W-DLK-33) (4k30)

**Might work, but untested**

- [DeckLink 4K Extreme 12G](https://www.blackmagicdesign.com/nl/products/decklink/techspecs/W-DLK-25) (4k60)

**Won't work**

The following cards have capable hardware but are not supported; getting them work is most likely possible but might involve high procurement and/or engineering costs. The rules for getting them added are simple: it needs a published API (either vendor-provided or reverse engineered) and I need the hardware or you send a pull-request.

- [AVerMedia CL511HN](https://www.avermedia.com/professional/product/cl511hn/overview) (4k60, pass-through) - There is an SDK available which looks usable
- [Magewell Pro Capture HDMI 4K Plus](https://www.magewell.com/products/pro-capture-hdmi-4k-plus) (4k60) - There is an SDK available
- [Magewell Pro Capture HDMI 4K Plus LT](https://www.magewell.com/products/pro-capture-hdmi-4k-plus) (4k60, pass-through) - There is an SDK available
- [AVerMedia Live Gamer 4K (GC573)](https://www.avermedia.com/us/product-detail/GC573) (4k60, pass-through) - No publicly available API/SDK
- [Elgato 4K60 pro](https://www.elgato.com/en/game-capture-4k60-pro) (4k60, pass-through) - No publicly available API/SDK



# System requirements

VideoProcessor itself takes very little CPU. The capture card drivers often just take a decent amount of memory up (gigs) and the rest is madVR. MadVR can be a massive resource drain; at maximum settings when working on a 4K high frame rate feed there simply is no available hardware which can sustain it (RTX3090 included).

Luckily if you tone it down a bit it works well with quite modest hardware. There are tons of threads like [Building a 4K HTPC for madVR](https://www.avsforum.com/threads/guide-building-a-4k-htpc-for-madvr.2364113/) on this so a bit of research will get you a long way.

(For reference, I'm developing/using it on Intel 11400 + 16GB ram + GTX 1660 +[BlackMagic DeckLink Mini Recorder 4k](https://www.blackmagicdesign.com/nl/products/decklink/techspecs/W-DLK-33)  which is enough for my purposes which is 4K HDR input, 1080p tone mapped output.)



# Dev stuff

Get the source from https://github.com/defl/videoprocessor

 * MSVC 2019 community edition

 * Install MFC libraries

 * Debug builds require the [Visual Leak Detector](https://kinddragon.github.io/vld/) Visual C++ plugin

   

**Debugging**

 * Directshow call debugging
    * Open regedit
    * Go to Computer\HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\DirectShow\Debug
    * Make key (=folder) VideoProcessor.exe
    * Set folder rights to user who will run it (or all)
    * Close regedit
    * Start application
    * Open regedit
    * Computer\HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\DirectShow\Debug\VideoProcessor.exe\ should have a bunch of entries like TRACE and LogToFile
    * Set log types to 5 or up

# Commercial alternatives

The people behind madVR also have a commercial offering called [MadVR Envy](https://madvrenvy.com/) It does what this program does but is a complete device, offers support, will be better, has more magic and it's HDCP certified meaning it can show HDCP protected content out of the box AND there is no DIY-ing involved. 

*If you have the means I would recommend you buy their product and support madVR development*

**Partial solutions**

madVR is unique in it's abilities and quality, nothing else comes close. For those of you who don't want everything and are on a budget (relatively speaking) the following might be of interest:

- Lumagen makes a device called the [Radiance Pro](http://www.lumagen.com/testindex.php?module=radiancepro_details) which can also do HDR tonemapping.
- Higher end JVC projectors can do HDR tonemapping internally.
- Higher end projectors and OLED TVs often can do 3DLUT internally.
- There are a lot of lower-end devices which can do 3dLUT, [DisplayCalibrations has a good overview of them](https://displaycalibrations.com/lut_boxes_comparisons.html).



# License & legal

This application is released under the GNU GPL 3.0, see LICENSE.txt. 

Parts of this code are made and owned by others, for example SDKs and the madVR interface; in all such cases there are LICENSE.txt and README.txt files present to point to sources, attributions and licenses.

I'm not affiliated or connected with any of the firms mentioned above. 



------

 Copyright 2021 [Dennis Fleurbaaij](mailto:mail@dennisfleurbaaij.com)



