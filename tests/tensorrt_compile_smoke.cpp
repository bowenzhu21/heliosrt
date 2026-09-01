#include <type_traits>

#include "helios/runtime/tensorrt_executor.hpp"

static_assert(std::is_base_of_v<helios::runtime::Executor, helios::runtime::TensorRtExecutor>);
static_assert(!std::is_copy_constructible_v<helios::runtime::TensorRtExecutor>);
static_assert(std::is_move_constructible_v<helios::runtime::TensorRtExecutor>);

int main() { return 0; }
