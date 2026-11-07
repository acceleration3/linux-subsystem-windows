#include "support/gl_backend.hpp"

#include "support/av_adapters.hpp"
#include "support/dxgi_budget.hpp"

namespace lsw::suites {

namespace {
constexpr unsigned kCopyRead = 0x8F36;
constexpr unsigned kCopyWrite = 0x8F37;
constexpr unsigned kDynamicDraw = 0x88E8;
constexpr unsigned kOutOfMemory = 0x0505;
}  // namespace

class BufferGl : public GpuBuffer {
public:
    ~BufferGl() override {
        if (backend != nullptr && id != 0) {
            backend->delete_buffers_(1, &id);
        }
    }

    GlBackend* backend = nullptr;
    unsigned id = 0;
    void* mapped = nullptr;
};

bool GlBackend::init(const LUID& luid, std::string& error) {
    luid_ = luid;
    std::vector<AvAdapter> adapters;
    if (!AvAdapters::all(adapters, error)) {
        return false;
    }
    TestWindowConfig config;
    if (const AvAdapter* adapter = AvAdapters::find(adapters, AvAdapters::key(luid))) {
        if (!adapter->outputs.empty()) {
            config.x = adapter->outputs.front().rect.left + 20;
            config.y = adapter->outputs.front().rect.top + 20;
        }
    }
    if (!window_.create(config, error) || !gl_.create(window_.dc(), error)) {
        return false;
    }
#define LSW_GL(field, name) field = reinterpret_cast<decltype(field)>(gl_.proc(name))
    LSW_GL(gen_buffers_, "glGenBuffers");
    LSW_GL(delete_buffers_, "glDeleteBuffers");
    LSW_GL(bind_buffer_, "glBindBuffer");
    LSW_GL(buffer_data_, "glBufferData");
    LSW_GL(buffer_sub_data_, "glBufferSubData");
    LSW_GL(get_buffer_sub_data_, "glGetBufferSubData");
    LSW_GL(copy_buffer_sub_data_, "glCopyBufferSubData");
    LSW_GL(buffer_storage_, "glBufferStorage");
    LSW_GL(map_buffer_range_, "glMapBufferRange");
    LSW_GL(unmap_buffer_, "glUnmapBuffer");
    get_error_ = reinterpret_cast<decltype(get_error_)>(gl_.export_symbol("glGetError"));
    finish_ = reinterpret_cast<decltype(finish_)>(gl_.export_symbol("glFinish"));
    if (gen_buffers_ == nullptr || buffer_data_ == nullptr || copy_buffer_sub_data_ == nullptr || get_buffer_sub_data_ == nullptr ||
        get_error_ == nullptr || finish_ == nullptr) {
        error = "the OpenGL context lacks buffer object entry points (" + gl_.version() + ")";
        return false;
    }
    persistent_ = buffer_storage_ != nullptr && map_buffer_range_ != nullptr && unmap_buffer_ != nullptr;
    return true;
}

std::unique_ptr<GpuBuffer> GlBackend::create(std::uint64_t size, BufferKind kind, AllocStatus& status,
                                             std::string& detail) {
    while (get_error_() != 0) {
    }
    auto buffer = std::make_unique<BufferGl>();
    buffer->backend = this;
    gen_buffers_(1, &buffer->id);
    bind_buffer_(kCopyWrite, buffer->id);
    if (kind == BufferKind::Device) {
        buffer_data_(kCopyWrite, static_cast<std::ptrdiff_t>(size), nullptr, kDynamicDraw);
    } else if (persistent_) {
        buffer_storage_(kCopyWrite, static_cast<std::ptrdiff_t>(size), nullptr, 0x100 | 0x1 | 0x2 | 0x40 | 0x80);
    } else {
        status = AllocStatus::Failed;
        detail = "persistent buffer storage is not available";
        return nullptr;
    }
    const unsigned error = get_error_();
    if (error != 0) {
        status = error == kOutOfMemory ? AllocStatus::OutOfMemory : AllocStatus::Failed;
        detail = "OpenGL error 0x" + std::to_string(error);
        return nullptr;
    }
    buffer->size = size;
    buffer->kind = kind;
    status = AllocStatus::Ok;
    return buffer;
}

bool GlBackend::map(GpuBuffer& buffer, void** pointer, std::string& error) {
    auto& b = static_cast<BufferGl&>(buffer);
    if (!persistent_ || b.kind == BufferKind::Device) {
        error = "persistent mapping is not available for this buffer";
        return false;
    }
    bind_buffer_(kCopyWrite, b.id);
    if (b.mapped == nullptr) {
        b.mapped = map_buffer_range_(kCopyWrite, 0, static_cast<std::ptrdiff_t>(b.size), 0x1 | 0x2 | 0x40 | 0x80);
    }
    if (b.mapped == nullptr) {
        error = "glMapBufferRange failed";
        return false;
    }
    *pointer = b.mapped;
    return true;
}

bool GlBackend::unmap(GpuBuffer& buffer, std::string&) {
    auto& b = static_cast<BufferGl&>(buffer);
    if (b.mapped != nullptr) {
        bind_buffer_(kCopyWrite, b.id);
        unmap_buffer_(kCopyWrite);
        b.mapped = nullptr;
    }
    return true;
}

bool GlBackend::upload(GpuBuffer& buffer, std::uint64_t offset, const void* data, std::uint64_t size, std::string& error) {
    auto& b = static_cast<BufferGl&>(buffer);
    bind_buffer_(kCopyWrite, b.id);
    buffer_sub_data_(kCopyWrite, static_cast<std::ptrdiff_t>(offset), static_cast<std::ptrdiff_t>(size), data);
    if (const unsigned code = get_error_(); code != 0) {
        error = "glBufferSubData raised 0x" + std::to_string(code);
        return false;
    }
    return true;
}

bool GlBackend::download(GpuBuffer& buffer, std::uint64_t offset, void* out, std::uint64_t size, std::string& error) {
    auto& b = static_cast<BufferGl&>(buffer);
    bind_buffer_(kCopyRead, b.id);
    get_buffer_sub_data_(kCopyRead, static_cast<std::ptrdiff_t>(offset), static_cast<std::ptrdiff_t>(size), out);
    if (const unsigned code = get_error_(); code != 0) {
        error = "glGetBufferSubData raised 0x" + std::to_string(code);
        return false;
    }
    return true;
}

bool GlBackend::copy(GpuBuffer& source, std::uint64_t source_offset, GpuBuffer& destination,
                     std::uint64_t destination_offset, std::uint64_t size, std::string& error) {
    bind_buffer_(kCopyRead, static_cast<BufferGl&>(source).id);
    bind_buffer_(kCopyWrite, static_cast<BufferGl&>(destination).id);
    copy_buffer_sub_data_(kCopyRead, kCopyWrite, static_cast<std::ptrdiff_t>(source_offset),
                          static_cast<std::ptrdiff_t>(destination_offset), static_cast<std::ptrdiff_t>(size));
    if (const unsigned code = get_error_(); code != 0) {
        error = "glCopyBufferSubData raised 0x" + std::to_string(code);
        return false;
    }
    return true;
}

bool GlBackend::finish(std::string&) {
    finish_();
    return true;
}

std::uint64_t GlBackend::available_memory() {
    VideoMemory memory;
    std::string error;
    if (!DxgiBudget::query(luid_, true, memory, error)) {
        return 0;
    }
    return memory.budget > memory.usage ? memory.budget - memory.usage : 0;
}

}  // namespace lsw::suites
