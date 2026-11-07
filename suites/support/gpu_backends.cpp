#include "support/d3d11_backend.hpp"
#include "support/d3d12_backend.hpp"
#include "support/d3d9_backend.hpp"
#include "support/gl_backend.hpp"
#include "support/gpu_backend.hpp"
#include "support/opencl_backend.hpp"
#include "support/vulkan_backend.hpp"

namespace lsw::suites {

std::vector<std::string> GpuBackends::names() {
    return {"d3d11", "d3d12", "vulkan", "opencl"};
}

std::unique_ptr<GpuBackend> GpuBackends::make(const std::string& name) {
    if (name == "d3d11") {
        return std::make_unique<D3d11Backend>();
    }
    if (name == "d3d12") {
        return std::make_unique<D3d12Backend>();
    }
    if (name == "vulkan") {
        return std::make_unique<VulkanBackend>();
    }
    if (name == "opencl") {
        return std::make_unique<OpenClBackend>();
    }
    if (name == "gl") {
        return std::make_unique<GlBackend>();
    }
    if (name == "d3d9") {
        return std::make_unique<D3d9Backend>();
    }
    return nullptr;
}

std::string GpuBackends::describe(AllocStatus status) {
    switch (status) {
    case AllocStatus::Ok:
        return "ok";
    case AllocStatus::OutOfMemory:
        return "out of memory";
    case AllocStatus::TooLarge:
        return "too large";
    case AllocStatus::Failed:
        return "failed";
    }
    return "?";
}

}  // namespace lsw::suites
