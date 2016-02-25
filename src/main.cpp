/*
    main.cpp -- HDRView application entry point

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/

#include <thread>
#include <cstdlib>
#include <iostream>
#include "HDRViewer.h"

/* Force usage of discrete GPU on laptops */
NANOGUI_FORCE_DISCRETE_GPU();

int nprocs = -1;

int main(int argc, char **argv)
{
    vector<string> args;
    bool help = false;
    float gamma = 2.2f, exposure = 0.0f;
    
#if defined(__APPLE__)
    bool launched_from_finder = false;
#endif

    try
    {
        for (int i=1; i<argc; ++i)
        {
            if (strcmp("--gamma", argv[i]) == 0 || strcmp("-g", argv[i]) == 0)
            {
                gamma = atof(argv[++i]);
                if (gamma < 0.f)
                    gamma = .1f;
                cout << "Gamma correction with g=" << gamma << endl;
            }
            else if (strcmp("--help", argv[i]) == 0 || strcmp("-h", argv[i]) == 0)
            {
                help = true;
            }
            else if (strcmp("--exposure", argv[i]) == 0 || strcmp("-e", argv[i]) == 0)
            {
                exposure = atof(argv[++i]);
                cout << "Intensity scale " << powf(2.0f, exposure) << endl;
#if defined(__APPLE__)
            }
            else if (strncmp("-psn", argv[i], 4) == 0)
            {
                launched_from_finder = true;
#endif
            }
            else
            {
                if (strncmp(argv[i], "-", 1) == 0)
                {
                    cerr << "Invalid argument: \"" << argv[i] << "\"!" << endl;
                    help = true;
                }
                args.push_back(argv[i]);
            }
        }
    }
    catch (const std::exception &e)
    {
        cout << "Error: " << e.what() << endl;
        help = true;
    }

    if (help)
    {
        cout << "Syntax: " << argv[0] << "[options]  <input file(s)>" << endl;
        cout << "Options:" << endl;
        cout << "   -g, --gamma <gamma>       Desired gamma value for exposure+gamma tonemapping" << endl;
        cout << "   -e, --exposure <exposure> Desired power of 2 exposure offset (intensity will be scaled by 2^exposure)" << endl;
        cout << "   -h, --help                Display this message" << endl;
        return -1;
    }

    if (args.size() == 0)
        cout << "Running in GUI mode, start with -h for instructions on batch mode." << endl;

    try
    {
        nanogui::init();

        #if defined(__APPLE__)
            if (launched_from_finder)
                nanogui::chdir_to_bundle_parent();
        #endif

        {
            nanogui::ref<HDRViewScreen> viewer = new HDRViewScreen(exposure, gamma, args);
            viewer->setVisible(true);
            nanogui::mainloop();
        }

        nanogui::shutdown();
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "Caught a fatal error: " << e.what() << endl;
        return -1;
    }

    return EXIT_SUCCESS;
}
