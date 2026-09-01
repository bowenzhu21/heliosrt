find_path(TensorRT_INCLUDE_DIR
  NAMES NvInfer.h
  HINTS ENV TensorRT_ROOT
  PATH_SUFFIXES include include/x86_64-linux-gnu)

find_library(TensorRT_NVINFER_LIBRARY
  NAMES nvinfer
  HINTS ENV TensorRT_ROOT
  PATH_SUFFIXES lib lib64 lib/x86_64-linux-gnu)

include(FindPackageHandleStandardArgs)
if(TensorRT_INCLUDE_DIR)
  file(STRINGS "${TensorRT_INCLUDE_DIR}/NvInferVersion.h" _tensorrt_version_lines
    REGEX "^#define NV_TENSORRT_(MAJOR|MINOR|PATCH) ")
  foreach(_component MAJOR MINOR PATCH)
    foreach(_line IN LISTS _tensorrt_version_lines)
      if(_line MATCHES "^#define NV_TENSORRT_${_component} +([0-9]+)")
        set(TensorRT_VERSION_${_component} "${CMAKE_MATCH_1}")
      endif()
    endforeach()
  endforeach()
  set(TensorRT_VERSION
    "${TensorRT_VERSION_MAJOR}.${TensorRT_VERSION_MINOR}.${TensorRT_VERSION_PATCH}")
endif()

find_package_handle_standard_args(TensorRT
  REQUIRED_VARS TensorRT_INCLUDE_DIR TensorRT_NVINFER_LIBRARY
  VERSION_VAR TensorRT_VERSION)

if(TensorRT_FOUND AND NOT TARGET TensorRT::nvinfer)
  if(TensorRT_VERSION_MAJOR LESS 10)
    message(FATAL_ERROR "HeliosRT requires TensorRT 10 or newer; found ${TensorRT_VERSION}")
  endif()

  add_library(TensorRT::nvinfer UNKNOWN IMPORTED)
  set_target_properties(TensorRT::nvinfer PROPERTIES
    IMPORTED_LOCATION "${TensorRT_NVINFER_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${TensorRT_INCLUDE_DIR}")
endif()

mark_as_advanced(TensorRT_INCLUDE_DIR TensorRT_NVINFER_LIBRARY)
