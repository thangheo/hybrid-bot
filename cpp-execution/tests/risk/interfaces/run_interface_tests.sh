#!/bin/bash
# Script to run all interface tests for the Risk component

# Set colored output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Set the directory where tests reside
TEST_DIR=./build/tests/risk/interfaces

# Check if build directory exists
if [ ! -d "$TEST_DIR" ]; then
    echo -e "${RED}Test directory not found. Make sure you've built the project.${NC}"
    echo "Run 'mkdir -p build && cd build && cmake .. && make' first."
    exit 1
fi

# Run the tests
echo -e "${YELLOW}Running Risk Interface Tests...${NC}"
echo "=============================================="

# Strategy Interface Tests
echo -e "${YELLOW}Running Strategy Interface Tests:${NC}"
$TEST_DIR/strategy_interface_test --gtest_color=yes
STRATEGY_RESULT=$?

# Execution Interface Tests
echo -e "\n${YELLOW}Running Execution Interface Tests:${NC}"
$TEST_DIR/execution_interface_test --gtest_color=yes
EXECUTION_RESULT=$?

# Data Interface Tests
echo -e "\n${YELLOW}Running Data Interface Tests:${NC}"
$TEST_DIR/data_interface_test --gtest_color=yes
DATA_RESULT=$?

# Print summary
echo -e "\n${YELLOW}Test Summary:${NC}"
echo "=============================================="

if [ $STRATEGY_RESULT -eq 0 ]; then
    echo -e "Strategy Interface Tests: ${GREEN}PASSED${NC}"
else
    echo -e "Strategy Interface Tests: ${RED}FAILED${NC}"
fi

if [ $EXECUTION_RESULT -eq 0 ]; then
    echo -e "Execution Interface Tests: ${GREEN}PASSED${NC}"
else
    echo -e "Execution Interface Tests: ${RED}FAILED${NC}"
fi

if [ $DATA_RESULT -eq 0 ]; then
    echo -e "Data Interface Tests: ${GREEN}PASSED${NC}"
else
    echo -e "Data Interface Tests: ${RED}FAILED${NC}"
fi

# Set overall result
if [ $STRATEGY_RESULT -eq 0 ] && [ $EXECUTION_RESULT -eq 0 ] && [ $DATA_RESULT -eq 0 ]; then
    echo -e "\n${GREEN}All tests passed successfully!${NC}"
    exit 0
else
    echo -e "\n${RED}Some tests failed.${NC}"
    exit 1
fi