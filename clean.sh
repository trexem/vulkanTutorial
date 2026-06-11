#!/usr/bin/env bash

BUILD_DIR="build"

if [ -d "$BUILD_DIR" ]; then
    echo "Cleaning $BUILD_DIR/ (preserving _deps/)..."
    
    # Find everything inside 'build' at depth 1 (files and folders)
    # and delete them, explicitly skipping the '_deps' directory.
    find "$BUILD_DIR" -maxdepth 1 -mindepth 1 ! -name "_deps" -exec rm -rf {} +
    
    echo "Cleanup complete!"
else
    echo "No build directory found to clean."
fi
