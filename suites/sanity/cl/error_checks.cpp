#include "sanity/cl/error_checks.hpp"

#include "sanity/cl/kit.hpp"

namespace lsw::suites::sanity::cl {

Outcome ErrorChecks::codes_documented(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        ClApi& api = rig.api;
        cl_int code = CL_SUCCESS;
        auto expect_null = [&](const char* what, auto* handle, cl_int got, std::initializer_list<cl_int> accepted) {
            if (handle != nullptr) {
                problems.add(std::string(what) + " returned an object");
            } else {
                Kit::expect(problems, what, got, accepted);
            }
        };
        cl_context_properties properties[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(rig.platform), 0};
        cl_context made = api.CreateContext(properties, 0, &rig.device, nullptr, nullptr, &code);
        expect_null("clCreateContext with zero devices", made, code, {CL_INVALID_VALUE});
        made = api.CreateContext(properties, 1, nullptr, nullptr, nullptr, &code);
        expect_null("clCreateContext with no device list", made, code, {CL_INVALID_VALUE});
        int user = 0;
        made = api.CreateContext(properties, 1, &rig.device, nullptr, &user, &code);
        expect_null("clCreateContext with user data but no callback", made, code, {CL_INVALID_VALUE});
        std::size_t info_size = 0;
        Kit::expect(problems, "clGetContextInfo(NULL)", api.GetContextInfo(nullptr, CL_CONTEXT_REFERENCE_COUNT, 0, nullptr, &info_size), {CL_INVALID_CONTEXT});
        Kit::expect(problems, "clGetDeviceInfo(NULL)", api.GetDeviceInfo(nullptr, CL_DEVICE_NAME, 0, nullptr, &info_size), {CL_INVALID_DEVICE});

        cl_mem buffer = api.CreateBuffer(nullptr, CL_MEM_READ_WRITE, 64, nullptr, &code);
        expect_null("clCreateBuffer with a NULL context", buffer, code, {CL_INVALID_CONTEXT});
        buffer = api.CreateBuffer(rig.context, CL_MEM_READ_WRITE, 0, nullptr, &code);
        expect_null("clCreateBuffer of size 0", buffer, code, {CL_INVALID_BUFFER_SIZE});
        buffer = api.CreateBuffer(rig.context, CL_MEM_READ_WRITE, static_cast<std::size_t>(rig.max_allocation()) + 1, nullptr, &code);
        expect_null("clCreateBuffer above CL_DEVICE_MAX_MEM_ALLOC_SIZE", buffer, code, {CL_INVALID_BUFFER_SIZE});
        buffer = api.CreateBuffer(rig.context, CL_MEM_READ_WRITE | CL_MEM_READ_ONLY, 64, nullptr, &code);
        expect_null("clCreateBuffer with READ_WRITE | READ_ONLY", buffer, code, {CL_INVALID_VALUE});
        std::uint32_t word = 0;
        buffer = api.CreateBuffer(rig.context, CL_MEM_READ_WRITE, 4, &word, &code);
        expect_null("clCreateBuffer with a host pointer and no host flag", buffer, code, {CL_INVALID_HOST_PTR});

        std::uint8_t bytes[16] = {};
        Kit::expect(problems, "clEnqueueReadBuffer(NULL queue)", api.EnqueueReadBuffer(nullptr, nullptr, CL_TRUE, 0, 4, bytes, 0, nullptr, nullptr), {CL_INVALID_COMMAND_QUEUE});
        Kit::expect(problems, "clEnqueueReadBuffer(NULL buffer)", api.EnqueueReadBuffer(rig.queue, nullptr, CL_TRUE, 0, 4, bytes, 0, nullptr, nullptr), {CL_INVALID_MEM_OBJECT});
        Kit::expect(problems, "clEnqueueNDRangeKernel(NULL queue)", api.EnqueueNDRangeKernel(nullptr, nullptr, 1, nullptr, nullptr, nullptr, 0, nullptr, nullptr), {CL_INVALID_COMMAND_QUEUE});
        std::size_t one = 1;
        Kit::expect(problems, "clEnqueueNDRangeKernel(NULL kernel)", api.EnqueueNDRangeKernel(rig.queue, nullptr, 1, nullptr, &one, nullptr, 0, nullptr, nullptr), {CL_INVALID_KERNEL});
        Kit::expect(problems, "clSetKernelArg(NULL kernel)", api.SetKernelArg(nullptr, 0, 4, &word), {CL_INVALID_KERNEL});
        cl_kernel kernel = api.CreateKernel(nullptr, "k", &code);
        expect_null("clCreateKernel(NULL program)", kernel, code, {CL_INVALID_PROGRAM});
        const char* text = "__kernel void k() {}";
        cl_program program = api.CreateProgramWithSource(nullptr, 1, &text, nullptr, &code);
        expect_null("clCreateProgramWithSource(NULL context)", program, code, {CL_INVALID_CONTEXT});
        program = api.CreateProgramWithSource(rig.context, 0, &text, nullptr, &code);
        expect_null("clCreateProgramWithSource with count 0", program, code, {CL_INVALID_VALUE});
        program = api.CreateProgramWithSource(rig.context, 1, nullptr, nullptr, &code);
        expect_null("clCreateProgramWithSource with no strings", program, code, {CL_INVALID_VALUE});

        cl_event event = nullptr;
        Kit::expect(problems, "clWaitForEvents with zero events", api.WaitForEvents(0, &event), {CL_INVALID_VALUE});
        Kit::expect(problems, "clWaitForEvents with a NULL list", api.WaitForEvents(1, nullptr), {CL_INVALID_VALUE});
        Kit::expect(problems, "clWaitForEvents with a NULL event", api.WaitForEvents(1, &event), {CL_INVALID_EVENT});
        cl_mem scratch = api.CreateBuffer(rig.context, CL_MEM_READ_WRITE, 64, nullptr, &code);
        ClOwned<cl_mem> owned(rig, scratch);
        if (scratch != nullptr) {
            Kit::expect(problems, "a wait list count of 1 with a NULL list", api.EnqueueWriteBuffer(rig.queue, scratch, CL_TRUE, 0, 4, bytes, 1, nullptr, nullptr), {CL_INVALID_EVENT_WAIT_LIST});
            Kit::expect(problems, "a wait list count of 0 with a list", api.EnqueueWriteBuffer(rig.queue, scratch, CL_TRUE, 0, 4, bytes, 0, &event, nullptr), {CL_INVALID_EVENT_WAIT_LIST});
        }
        Kit::expect(problems, "clReleaseMemObject(NULL)", api.ReleaseMemObject(nullptr), {CL_INVALID_MEM_OBJECT});
        Kit::expect(problems, "clReleaseKernel(NULL)", api.ReleaseKernel(nullptr), {CL_INVALID_KERNEL});
        Kit::expect(problems, "clReleaseProgram(NULL)", api.ReleaseProgram(nullptr), {CL_INVALID_PROGRAM});
        Kit::expect(problems, "clReleaseEvent(NULL)", api.ReleaseEvent(nullptr), {CL_INVALID_EVENT});
        Kit::expect(problems, "clReleaseCommandQueue(NULL)", api.ReleaseCommandQueue(nullptr), {CL_INVALID_COMMAND_QUEUE});
        Kit::expect(problems, "clReleaseContext(NULL)", api.ReleaseContext(nullptr), {CL_INVALID_CONTEXT});
        Kit::expect(problems, "clFinish(NULL)", api.Finish(nullptr), {CL_INVALID_COMMAND_QUEUE});

        if (rig.version.at_least(2, 0) && api.CreateCommandQueueWithProperties != nullptr) {
            const cl_queue_properties bad[] = {0x7777, 1, 0};
            cl_command_queue queue = api.CreateCommandQueueWithProperties(rig.context, rig.device, bad, &code);
            expect_null("clCreateCommandQueueWithProperties with an unknown property", queue, code, {CL_INVALID_VALUE, CL_INVALID_QUEUE_PROPERTIES});
            queue = api.CreateCommandQueueWithProperties(nullptr, rig.device, nullptr, &code);
            expect_null("clCreateCommandQueueWithProperties(NULL context)", queue, code, {CL_INVALID_CONTEXT});
            queue = api.CreateCommandQueueWithProperties(rig.context, nullptr, nullptr, &code);
            expect_null("clCreateCommandQueueWithProperties(NULL device)", queue, code, {CL_INVALID_DEVICE});
        }
        Kit::Pair pair;
        if (Kit::vector_add(rig, 4096, 3300, pair, problems, "after the invalid calls")) {
            Kit::same_words(context, problems, "vector add after the invalid calls", pair.expected, pair.actual);
        }
        return problems.outcome();
    });
}

}  // namespace lsw::suites::sanity::cl
