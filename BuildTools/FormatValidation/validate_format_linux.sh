#!/bin/bash

source ../../../DiligentCore/BuildTools/FormatValidation/validate_format_linux_implementation.sh

validate_format ../../Components ../../PBR ../../PostProcess ../../Utilities ../../Hydrogent ../../Tests \
-e ../../Hydrogent/shaders_inc
