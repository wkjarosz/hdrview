cmake_minimum_required(VERSION 3.1)

# Add a symlink to /usr/local/bin so we can launch HDRView from the commandline
execute_process(COMMAND rm -f /usr/local/bin/hdrview)
execute_process(COMMAND ln -s /Applications/HDRView.app/Contents/MacOS/HDRView /usr/local/bin/hdrview)
