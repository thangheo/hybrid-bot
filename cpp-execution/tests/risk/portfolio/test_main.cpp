/**
 * Test main file
 * 
 * File: test_main.cpp
 * Created: 2025-04-30
 */

 #include <gtest/gtest.h>

 int main(int argc, char **argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }