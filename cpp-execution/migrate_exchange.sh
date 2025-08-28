#!/bin/bash
# Find all the messy definitions
echo "=== Finding ExchangeId definitions ==="
grep -r "using ExchangeId.*string\|typedef.*ExchangeId\|enum.*ExchangeId\|struct ExchangeId" \
  include/ src/ --include="*.hpp" --include="*.cpp" -n

# Replace them all
find include/ src/ -name "*.hpp" -o -name "*.cpp" | xargs sed -i \
  's/using ExchangeId = std::string;/#include "common\/exchange_types.hpp"/g'