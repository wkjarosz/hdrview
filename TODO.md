## Todo/Wishlist

* allow error logging to output to a debug status panel in the GUI.
* add basic HDR merging capabilities (i.e. assuming registered, sRGB images)
* add image processing operations (e.g. filtering, resizing) to GUI mode
* add support for resizing layer panel (once resizing support is added to nanogui)
* add basic selection support with min/max/mean/std statistics
* enable processing/filtering images passed on command-line even in GUI mode (e.g. load many images, blur them, and then display them in the GUI, possibly without saving)
* long-term: add simple painting/clone-stamp support


## Known bugs
* ``stb_image`` seemingly does not properly handle sRGB's non-gamma curve. loading an sRGB image, saving as sRGB, and loading back in as sRGB should result in same image but doesn't (at least for PNGs). ``stb_image`` seems to assume just a simple gamma curve for sRGB (defaulting to gamma=2.2), and excludes the linear regions near black
