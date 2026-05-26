#include <gtest/gtest.h>
#include <dlfcn.h>
#include <string>

// Test if we can call diagnostic functions from the interior_test_node plugin
TEST(PluginDiagnostics, CanCallAllocateFunction) {
    // Load the plugin
    std::string plugin_path = "/Users/rklinkhammer/workspace/GraphX/build/plugins/libinterior_test_node.so";
    void* handle = dlopen(plugin_path.c_str(), RTLD_LAZY);
    ASSERT_NE(nullptr, handle) << "Failed to load plugin: " << dlerror();
    
    // Get the allocation test function
    typedef void* (*AllocFunc)();
    AllocFunc test_alloc = (AllocFunc)dlsym(handle, "plugin_test_allocate_interior");
    ASSERT_NE(nullptr, test_alloc) << "Failed to find plugin_test_allocate_interior: " << dlerror();
    
    // Try to call it
    void* result = test_alloc();
    EXPECT_NE(nullptr, result);
    
    dlclose(handle);
}

TEST(PluginDiagnostics, CanCallSharedPtrFunction) {
    // Load the plugin
    std::string plugin_path = "/Users/rklinkhammer/workspace/GraphX/build/plugins/libinterior_test_node.so";
    void* handle = dlopen(plugin_path.c_str(), RTLD_LAZY);
    ASSERT_NE(nullptr, handle) << "Failed to load plugin: " << dlerror();
    
    // Get the shared_ptr test function
    typedef void* (*SharedPtrFunc)();
    SharedPtrFunc test_shared_ptr = (SharedPtrFunc)dlsym(handle, "plugin_test_shared_ptr_interior");
    ASSERT_NE(nullptr, test_shared_ptr) << "Failed to find plugin_test_shared_ptr_interior: " << dlerror();
    
    // Try to call it
    void* result = test_shared_ptr();
    EXPECT_NE(nullptr, result);
    
    dlclose(handle);
}

TEST(PluginDiagnostics, CanCallCreateFunction) {
    // Load the plugin
    std::string plugin_path = "/Users/rklinkhammer/workspace/GraphX/build/plugins/libinterior_test_node.so";
    void* handle = dlopen(plugin_path.c_str(), RTLD_LAZY);
    ASSERT_NE(nullptr, handle) << "Failed to load plugin: " << dlerror();
    
    // Get the create function
    typedef void* (*CreateFunc)();
    CreateFunc test_create = (CreateFunc)dlsym(handle, "plugin_create_interior_test_node");
    ASSERT_NE(nullptr, test_create) << "Failed to find plugin_create_interior_test_node: " << dlerror();
    
    // Try to call it
    void* result = test_create();
    EXPECT_NE(nullptr, result);
    
    dlclose(handle);
}
