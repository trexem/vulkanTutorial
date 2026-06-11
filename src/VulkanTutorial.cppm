module;

#include <cstdlib>
#include <cstring>  // For strcmp
#include <exception>

export module vulkan_tutorial;
#if defined(__INTELLISENSE__)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan;
import std;
#endif
#include <GLFW/glfw3.h>

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;

export class VulkanTutorial {
 public:
  void run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

 private:
  void initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
  }

  void initVulkan() { createInstance(); }

  void createInstance() {
    constexpr vk::ApplicationInfo appInfo{
        .pApplicationName = "VulkanTutorial",
        .applicationVersion = vk::makeVersion(1, 0, 0),
        .pEngineName = "Trexem Engine",
        .engineVersion = vk::makeVersion(1, 0, 0),
        .apiVersion = vk::ApiVersion14};

    uint32_t glfwExtensionCount = 0;
    auto glfwExtensions =
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    auto extensionProperties = context.enumerateInstanceExtensionProperties();
    for (uint32_t i = 0; i < glfwExtensionCount; ++i) {
      if (std::ranges::none_of(
              extensionProperties, [glfwExtension = glfwExtensions[i]](
                                       auto const& extensionProperty) {
                return strcmp(extensionProperty.extensionName, glfwExtension) ==
                       0;
              })) {
        throw std::runtime_error("Required GLFW extension not supported: " +
                                 std::string(glfwExtensions[i]));
      }
    }

    vk::InstanceCreateInfo createInfo{
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = glfwExtensionCount,
        .ppEnabledExtensionNames = glfwExtensions};

    instance = vk::raii::Instance(context, createInfo);
  }

  void mainLoop() {
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
    }
  }

  void cleanup() {
    if (window) {
      glfwDestroyWindow(window);
    }
    glfwTerminate();
  }

  GLFWwindow* window = nullptr;

  vk::raii::Context context;
  vk::raii::Instance instance = nullptr;
};
