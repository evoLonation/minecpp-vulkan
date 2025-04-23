# !/bin/zsh

set -e # Exit immediately if a command exits with a non-zero status.

curl https://raw.githubusercontent.com/nothings/stb/refs/heads/master/stb_image.h -o stb_image.h

mv stb_image.h ../third_party/include/
