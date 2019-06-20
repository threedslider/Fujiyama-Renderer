# Copyright (c) 2011-2019 Hiroshi Tsubokawa
# See LICENSE and README

Build Fujiyama-Renderer-for-Win x64
===================================

  setupenv_vs.bat
  nmake

  or

  setupenv_vs.bat
  nmake INCLUDE_PATH=C:\your_include_path LIBRARY_PATH=C:\your_lib_path

  The default paths are:
    INCLUDE_PATH =C:\include
    LIBRARY_PATH=C:\lib
  Set these paths to look up OpenGL, OpenEXR, JPEG libraries installed on your computer.
