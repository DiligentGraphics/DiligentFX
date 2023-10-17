#!/bin/bash
python3 ../../../DiligentCore/BuildTools/FormatValidation/clang-format-validate.py \
--clang-format-executable ../../../DiligentCore/BuildTools/FormatValidation/clang-format_mac_10.0.0 \
-r ../../Components ../../PBR ../../PostProcess ../../Utilities ../../Hydrogent ../../Tests \
-e ../../Hydrogent/shaders_inc
