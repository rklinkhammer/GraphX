# CMake generated Testfile for 
# Source directory: /Users/rklinkhammer/workspace/GraphX/libgraph/test
# Build directory: /Users/rklinkhammer/workspace/GraphX/build-dsp-clean/libgraph/test
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[libgraph_unit]=] "/Users/rklinkhammer/workspace/GraphX/build-dsp-clean/libgraph/test/test_libgraph_unit")
set_tests_properties([=[libgraph_unit]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/rklinkhammer/workspace/GraphX/libgraph/test/CMakeLists.txt;41;add_test;/Users/rklinkhammer/workspace/GraphX/libgraph/test/CMakeLists.txt;0;")
add_test([=[libgraph_integration]=] "/Users/rklinkhammer/workspace/GraphX/build-dsp-clean/libgraph/test/test_libgraph_integration")
set_tests_properties([=[libgraph_integration]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/rklinkhammer/workspace/GraphX/libgraph/test/CMakeLists.txt;60;add_test;/Users/rklinkhammer/workspace/GraphX/libgraph/test/CMakeLists.txt;0;")
subdirs("plugins")
