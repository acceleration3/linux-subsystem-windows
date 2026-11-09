#include "render/vk/rig.hpp"

#include <cstring>

namespace lsw::render::vk {

namespace {

const float kClear[4] = {0.05f, 0.06f, 0.1f, 1.0f};

std::string sparse_caps(Kit& kit) {
    std::uint32_t count = 0;
    kit.api.vkGetPhysicalDeviceQueueFamilyProperties(kit.physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    kit.api.vkGetPhysicalDeviceQueueFamilyProperties(kit.physical, &count, families.data());
    return (families[kit.family].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) != 0 ? std::string() : std::string("sparse binding queue");
}

class SparseVertices {
public:
    SparseVertices(Rig& rig, std::uint32_t pages) : rig_(rig), pages_(pages) {
        Kit& kit = rig.kit;
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.flags = VK_BUFFER_CREATE_SPARSE_BINDING_BIT | VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT;
        info.size = 4096;
        info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer probe = VK_NULL_HANDLE;
        rig.vk_ok(kit.api.vkCreateBuffer(kit.device, &info, nullptr, &probe), "vkCreateBuffer sparse");
        VkMemoryRequirements req{};
        kit.api.vkGetBufferMemoryRequirements(kit.device, probe, &req);
        kit.api.vkDestroyBuffer(kit.device, probe, nullptr);
        page_ = req.alignment;
        type_ = kit.memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        rig.must(type_ != ~0u, "no memory type for a sparse buffer");
        info.size = page_ * pages;
        rig.vk_ok(kit.api.vkCreateBuffer(kit.device, &info, nullptr, &buffer_), "vkCreateBuffer sparse");
        const VkBuffer handle = buffer_;
        kit.defer([&kit, handle] { kit.api.vkDestroyBuffer(kit.device, handle, nullptr); });
    }

    [[nodiscard]] VkDeviceSize page() const { return page_; }
    [[nodiscard]] Buffer buffer() const {
        Buffer b;
        b.buffer = buffer_;
        b.size = page_ * pages_;
        return b;
    }

    void bind(std::uint32_t page, VkDeviceMemory memory) {
        Kit& kit = rig_.kit;
        VkSparseMemoryBind bind{};
        bind.resourceOffset = page_ * page;
        bind.size = page_;
        bind.memory = memory;
        VkSparseBufferMemoryBindInfo buffer_bind{buffer_, 1, &bind};
        VkBindSparseInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
        info.bufferBindCount = 1;
        info.pBufferBinds = &buffer_bind;
        rig_.vk_ok(kit.api.vkQueueBindSparse(kit.queue, 1, &info, VK_NULL_HANDLE), "vkQueueBindSparse");
        rig_.vk_ok(kit.api.vkQueueWaitIdle(kit.queue), "vkQueueWaitIdle");
    }

    VkDeviceMemory memory() {
        Kit& kit = rig_.kit;
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = page_;
        alloc.memoryTypeIndex = type_;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        rig_.vk_ok(kit.api.vkAllocateMemory(kit.device, &alloc, nullptr, &mem), "vkAllocateMemory");
        kit.defer([&kit, mem] { kit.api.vkFreeMemory(kit.device, mem, nullptr); });
        return mem;
    }

    void fill(std::uint32_t page, const std::vector<Vertex2>& vertices) {
        Kit& kit = rig_.kit;
        const Buffer staging = rig_.buffer(vertices, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        const VkDeviceSize at = page_ * page;
        rig_.run([&](VkCommandBuffer cb) {
            kit.api.vkCmdFillBuffer(cb, buffer_, at, page_, 0);
            VkMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            kit.api.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &b, 0, nullptr, 0, nullptr);
            const VkBufferCopy copy{0, at, vertices.size() * sizeof(Vertex2)};
            kit.api.vkCmdCopyBuffer(cb, staging.buffer, buffer_, 1, &copy);
        });
    }

private:
    Rig& rig_;
    std::uint32_t pages_;
    VkDeviceSize page_ = 0;
    std::uint32_t type_ = ~0u;
    VkBuffer buffer_ = VK_NULL_HANDLE;
};

std::vector<Vertex2> triangle(float x, float y, float size, unsigned color) {
    return {vertex(x, y, color), vertex(x + size, y, color + 1), vertex(x + size * 0.5f, y + size, color + 2)};
}

void register_sparse_buffers() {
    CaseSpec pages("tex", "sparse_buffer_vertex_pages");
    pages.feature("sparseBinding").feature("sparseResidencyBuffer").tolerance(Tolerance::exact());
    Cases::add(
        std::move(pages),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            Target& target = rig.make_target(TargetDesc{});
            std::copy(kClear, kClear + 4, target.clear_color);
            SparseVertices sparse(rig, 4);
            const VkDeviceMemory first = sparse.memory();
            const VkDeviceMemory third = sparse.memory();
            sparse.bind(0, first);
            sparse.bind(2, third);
            std::vector<Vertex2> page0 = triangle(-0.9f, -0.8f, 0.8f, 0);
            const std::vector<Vertex2> more = triangle(-0.4f, 0.1f, 0.7f, 3);
            page0.insert(page0.end(), more.begin(), more.end());
            sparse.fill(0, page0);
            sparse.fill(2, triangle(0.1f, -0.7f, 0.85f, 5));
            const Buffer vb = sparse.buffer();
            const VkPipeline pipeline = rig.pipe(rig.pos2c(target));
            out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                rig.bind_vertex(cb, vb, 0);
                kit.api.vkCmdDraw(cb, 6, 1, 0, 0);
                rig.bind_vertex(cb, vb, sparse.page() * 2);
                kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            }));
        },
        sparse_caps);
    class Rebind : public RigScene {
    protected:
        std::string caps(Kit& kit) override {
            return sparse_caps(kit);
        }

        void init(Rig& rig) override {
            target_ = &rig.make_target(TargetDesc{});
            std::copy(kClear, kClear + 4, target_->clear_color);
            sparse_ = std::make_unique<SparseVertices>(rig, 3);
            fixed_ = sparse_->memory();
            sparse_->bind(0, fixed_);
            sparse_->fill(0, triangle(-0.9f, -0.9f, 0.7f, 0));
            pipeline_ = rig.pipe(rig.pos2c(*target_));
        }

        void step(Rig& rig, unsigned frame) override {
            Kit& kit = rig.kit;
            if (frame < 3) {
                const VkDeviceMemory memory = sparse_->memory();
                sparse_->bind(1, memory);
                sparse_->fill(1, triangle(-0.2f + 0.25f * static_cast<float>(frame), -0.5f + 0.35f * static_cast<float>(frame), 0.8f, 2 + frame));
            } else {
                sparse_->bind(1, VK_NULL_HANDLE);
            }
            const Buffer vb = sparse_->buffer();
            image_ = rig.draw(*target_, pipeline_, nullptr, [&](VkCommandBuffer cb) {
                rig.bind_vertex(cb, vb, 0);
                kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                if (frame < 3) {
                    rig.bind_vertex(cb, vb, sparse_->page());
                    kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                }
            });
        }

        void grab(Rig&, unsigned, CaptureSet& out) override { out.add("", image_); }

    private:
        Target* target_ = nullptr;
        std::unique_ptr<SparseVertices> sparse_;
        VkDeviceMemory fixed_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
        Image image_;
    };
    CaseSpec rebind("tex", "sparse_buffer_rebind_frames");
    rebind.feature("sparseBinding").feature("sparseResidencyBuffer").tolerance(Tolerance::exact()).frames(4, {0, 1, 2, 3});
    Cases::add_class<Rebind>(std::move(rebind));
}

const int registered = (register_sparse_buffers(), 0);

}  // namespace

}  // namespace lsw::render::vk
