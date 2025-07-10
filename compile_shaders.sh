#!/bin/bash

find ./shaders -type f ! -name "*.spv" -exec glslc {} -o {}.spv \;
