from pathlib import Path

path = Path('tests/runtime/CMakeLists.txt')
text = path.read_text()
marker = 'vice_runtime_resource_residency_tests'
if marker in text:
    raise SystemExit('resource residency test already registered')
block = '''\nadd_executable(\n  vice_runtime_resource_residency_tests\n  ../runtime_resource_residency_tests.cpp\n)\ntarget_link_libraries(\n  vice_runtime_resource_residency_tests\n  PRIVATE\n    VICE::Runtime\n)\ntarget_compile_features(vice_runtime_resource_residency_tests PRIVATE cxx_std_23)\nvice_enable_warnings(vice_runtime_resource_residency_tests)\nadd_test(\n  NAME vice_runtime_resource_residency_tests\n  COMMAND vice_runtime_resource_residency_tests\n)\n'''
path.write_text(text.rstrip() + '\n' + block)
