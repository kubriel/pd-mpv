This is pd-mpv updated for libmpv render api.
all credits to Antoine Villeret,  IOhannes m zmölnig
# gem-mpv

Bring the power of `libmpv` into `Gem`.

## Building

You need a recent version of Gem installed locally and the libmpv at least version 0.27.2.

    git clone https://github.com/kubriel/pd-mpv.git
    mkdir build
    cd build
    cmake ../
    cmake --build .

Then you will find a `gem-mpv.pd_linux` in the `build` folder.
Put it in your Pd search path and start playing around.

Feel free to report issue on the tracker : https://github.com/kubriel/pd-mpv/issues
