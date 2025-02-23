/**
    \file colormap.h
*/
#pragma once

#include "fwd.h"
#include "implot.h"
#include <vector>

using Colormap_ = int;
enum EColormap : Colormap_
{
    Colormap_Deep     = ImPlotColormap_Deep,     // a.k.a. seaborn deep             (qual=true,  n=10) (default)
    Colormap_Dark     = ImPlotColormap_Dark,     // a.k.a. matplotlib "Set1"        (qual=true,  n=9 )
    Colormap_Pastel   = ImPlotColormap_Pastel,   // a.k.a. matplotlib "Pastel1"     (qual=true,  n=9 )
    Colormap_Paired   = ImPlotColormap_Paired,   // a.k.a. matplotlib "Paired"      (qual=true,  n=12)
    Colormap_Viridis  = ImPlotColormap_Viridis,  // a.k.a. matplotlib "viridis"     (qual=false, n=11)
    Colormap_Plasma   = ImPlotColormap_Plasma,   // a.k.a. matplotlib "plasma"      (qual=false, n=11)
    Colormap_Hot      = ImPlotColormap_Hot,      // a.k.a. matplotlib/MATLAB "hot"  (qual=false, n=11)
    Colormap_Cool     = ImPlotColormap_Cool,     // a.k.a. matplotlib/MATLAB "cool" (qual=false, n=11)
    Colormap_Pink     = ImPlotColormap_Pink,     // a.k.a. matplotlib/MATLAB "pink" (qual=false, n=11)
    Colormap_Jet      = ImPlotColormap_Jet,      // a.k.a. MATLAB "jet"             (qual=false, n=11)
    Colormap_Twilight = ImPlotColormap_Twilight, // a.k.a. matplotlib "twilight"    (qual=false, n=11)
    Colormap_RdBu     = ImPlotColormap_RdBu,     // red/blue, Color Brewer          (qual=false, n=11)
    Colormap_BrBG     = ImPlotColormap_BrBG,     // brown/blue-green, Color Brewer  (qual=false, n=11)
    Colormap_PiYG     = ImPlotColormap_PiYG,     // pink/yellow-green, Color Brewer (qual=false, n=11)
    Colormap_Spectral = ImPlotColormap_Spectral, // color spectrum, Color Brewer    (qual=false, n=11)
    Colormap_Greys    = ImPlotColormap_Greys,    // white/black                     (qual=false, n=2 )
    Colormap_Inferno,
    Colormap_Turbo,
    Colormap_IceFire,
    Colormap_CoolWarm,
    Colormap_COUNT
};

class Colormap
{
public:
    static void initialize();
    static void cleanup();

    static const char                *name(Colormap_ idx);
    static Texture                   *texture(Colormap_ idx);
    static const std::vector<float4> &values(Colormap_ idx);
};
