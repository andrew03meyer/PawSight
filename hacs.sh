#!/bin/bash
if [ ! -d "/config/custom_components/hacs" ]; then
  echo "Installing HACS..."
  wget -O - https://get.hacs.xyz | bash -
else
  echo "HACS already installed, skipping."
fi