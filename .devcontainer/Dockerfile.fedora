FROM fedora:41

RUN dnf install -y \
  # development tools
  git astyle \
  # build toolchain
  clang-18 ccache cmake ninja-build mold \
  # libraries
  SDL2-devel SDL2_image-devel SDL2_ttf-devel SDL2_mixer-devel \
  freetype glibc bzip2 zlib-ng libvorbis ncurses gettext flac-devel

RUN curl -fsSL https://deno.land/install.sh | sh
