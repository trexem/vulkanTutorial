#include <cstdlib>
#include <exception>

// #include "VulkanTutorial.hpp"

#if !defined(__INTELLISENSE__)
import std;
import vulkan_tutorial;
#else
#include <iostream>
#endif

int main() {
  try {
    VulkanTutorial app;
    app.run();
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
