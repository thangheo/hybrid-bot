#!/bin/bash

# Check if directory is provided
if [ -z "$1" ]; then
    echo "Usage: $0 <directory-path>"
    exit 1
fi

TARGET_DIR="$1"

# Check if the given path is a directory
if [ ! -d "$TARGET_DIR" ]; then
    echo "Error: '$TARGET_DIR' is not a valid directory."
    exit 1
fi

# Perform renaming
find "$TARGET_DIR" -type f -name "*.h" | while read -r file; do
    newfile="${file%.h}.hpp"
    echo "Renaming: $file -> $newfile"
    mv "$file" "$newfile"
done

