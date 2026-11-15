#include "support/cl_tools.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace lsw::suites {

namespace {

struct Named {
    cl_int code;
    const char* name;
};

const Named kNames[] = {
    {CL_SUCCESS, "CL_SUCCESS"},
    {CL_DEVICE_NOT_FOUND, "CL_DEVICE_NOT_FOUND"},
    {CL_DEVICE_NOT_AVAILABLE, "CL_DEVICE_NOT_AVAILABLE"},
    {CL_COMPILER_NOT_AVAILABLE, "CL_COMPILER_NOT_AVAILABLE"},
    {CL_MEM_OBJECT_ALLOCATION_FAILURE, "CL_MEM_OBJECT_ALLOCATION_FAILURE"},
    {CL_OUT_OF_RESOURCES, "CL_OUT_OF_RESOURCES"},
    {CL_OUT_OF_HOST_MEMORY, "CL_OUT_OF_HOST_MEMORY"},
    {CL_PROFILING_INFO_NOT_AVAILABLE, "CL_PROFILING_INFO_NOT_AVAILABLE"},
    {CL_MEM_COPY_OVERLAP, "CL_MEM_COPY_OVERLAP"},
    {CL_IMAGE_FORMAT_MISMATCH, "CL_IMAGE_FORMAT_MISMATCH"},
    {CL_IMAGE_FORMAT_NOT_SUPPORTED, "CL_IMAGE_FORMAT_NOT_SUPPORTED"},
    {CL_BUILD_PROGRAM_FAILURE, "CL_BUILD_PROGRAM_FAILURE"},
    {CL_MAP_FAILURE, "CL_MAP_FAILURE"},
    {CL_MISALIGNED_SUB_BUFFER_OFFSET, "CL_MISALIGNED_SUB_BUFFER_OFFSET"},
    {CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST, "CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST"},
    {CL_COMPILE_PROGRAM_FAILURE, "CL_COMPILE_PROGRAM_FAILURE"},
    {CL_LINKER_NOT_AVAILABLE, "CL_LINKER_NOT_AVAILABLE"},
    {CL_LINK_PROGRAM_FAILURE, "CL_LINK_PROGRAM_FAILURE"},
    {CL_DEVICE_PARTITION_FAILED, "CL_DEVICE_PARTITION_FAILED"},
    {CL_KERNEL_ARG_INFO_NOT_AVAILABLE, "CL_KERNEL_ARG_INFO_NOT_AVAILABLE"},
    {CL_INVALID_VALUE, "CL_INVALID_VALUE"},
    {CL_INVALID_DEVICE_TYPE, "CL_INVALID_DEVICE_TYPE"},
    {CL_INVALID_PLATFORM, "CL_INVALID_PLATFORM"},
    {CL_INVALID_DEVICE, "CL_INVALID_DEVICE"},
    {CL_INVALID_CONTEXT, "CL_INVALID_CONTEXT"},
    {CL_INVALID_QUEUE_PROPERTIES, "CL_INVALID_QUEUE_PROPERTIES"},
    {CL_INVALID_COMMAND_QUEUE, "CL_INVALID_COMMAND_QUEUE"},
    {CL_INVALID_HOST_PTR, "CL_INVALID_HOST_PTR"},
    {CL_INVALID_MEM_OBJECT, "CL_INVALID_MEM_OBJECT"},
    {CL_INVALID_IMAGE_FORMAT_DESCRIPTOR, "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR"},
    {CL_INVALID_IMAGE_SIZE, "CL_INVALID_IMAGE_SIZE"},
    {CL_INVALID_SAMPLER, "CL_INVALID_SAMPLER"},
    {CL_INVALID_BINARY, "CL_INVALID_BINARY"},
    {CL_INVALID_BUILD_OPTIONS, "CL_INVALID_BUILD_OPTIONS"},
    {CL_INVALID_PROGRAM, "CL_INVALID_PROGRAM"},
    {CL_INVALID_PROGRAM_EXECUTABLE, "CL_INVALID_PROGRAM_EXECUTABLE"},
    {CL_INVALID_KERNEL_NAME, "CL_INVALID_KERNEL_NAME"},
    {CL_INVALID_KERNEL_DEFINITION, "CL_INVALID_KERNEL_DEFINITION"},
    {CL_INVALID_KERNEL, "CL_INVALID_KERNEL"},
    {CL_INVALID_ARG_INDEX, "CL_INVALID_ARG_INDEX"},
    {CL_INVALID_ARG_VALUE, "CL_INVALID_ARG_VALUE"},
    {CL_INVALID_ARG_SIZE, "CL_INVALID_ARG_SIZE"},
    {CL_INVALID_KERNEL_ARGS, "CL_INVALID_KERNEL_ARGS"},
    {CL_INVALID_WORK_DIMENSION, "CL_INVALID_WORK_DIMENSION"},
    {CL_INVALID_WORK_GROUP_SIZE, "CL_INVALID_WORK_GROUP_SIZE"},
    {CL_INVALID_WORK_ITEM_SIZE, "CL_INVALID_WORK_ITEM_SIZE"},
    {CL_INVALID_GLOBAL_OFFSET, "CL_INVALID_GLOBAL_OFFSET"},
    {CL_INVALID_EVENT_WAIT_LIST, "CL_INVALID_EVENT_WAIT_LIST"},
    {CL_INVALID_EVENT, "CL_INVALID_EVENT"},
    {CL_INVALID_OPERATION, "CL_INVALID_OPERATION"},
    {CL_INVALID_GL_OBJECT, "CL_INVALID_GL_OBJECT"},
    {CL_INVALID_BUFFER_SIZE, "CL_INVALID_BUFFER_SIZE"},
    {CL_INVALID_MIP_LEVEL, "CL_INVALID_MIP_LEVEL"},
    {CL_INVALID_GLOBAL_WORK_SIZE, "CL_INVALID_GLOBAL_WORK_SIZE"},
    {CL_INVALID_PROPERTY, "CL_INVALID_PROPERTY"},
    {CL_INVALID_IMAGE_DESCRIPTOR, "CL_INVALID_IMAGE_DESCRIPTOR"},
    {CL_INVALID_COMPILER_OPTIONS, "CL_INVALID_COMPILER_OPTIONS"},
    {CL_INVALID_LINKER_OPTIONS, "CL_INVALID_LINKER_OPTIONS"},
    {CL_INVALID_DEVICE_PARTITION_COUNT, "CL_INVALID_DEVICE_PARTITION_COUNT"},
    {CL_INVALID_PIPE_SIZE, "CL_INVALID_PIPE_SIZE"},
    {CL_INVALID_DEVICE_QUEUE, "CL_INVALID_DEVICE_QUEUE"},
    {CL_INVALID_SPEC_ID, "CL_INVALID_SPEC_ID"},
    {CL_MAX_SIZE_RESTRICTION_EXCEEDED, "CL_MAX_SIZE_RESTRICTION_EXCEEDED"},
    {-1001, "CL_PLATFORM_NOT_FOUND_KHR"},
};

}  // namespace

std::string ClTools::error_name(cl_int code) {
    for (const Named& named : kNames) {
        if (named.code == code) {
            return std::string(named.name) + " (" + std::to_string(code) + ")";
        }
    }
    return "error " + std::to_string(code);
}

bool ClTools::parse_version(const std::string& text, const std::string& prefix, ClVersion& out, std::string* rest) {
    if (text.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    std::size_t at = prefix.size();
    if (at >= text.size() || !std::isdigit(static_cast<unsigned char>(text[at]))) {
        return false;
    }
    char* end = nullptr;
    const long major = std::strtol(text.c_str() + at, &end, 10);
    if (end == nullptr || *end != '.' || !std::isdigit(static_cast<unsigned char>(end[1]))) {
        return false;
    }
    char* end_minor = nullptr;
    const long minor = std::strtol(end + 1, &end_minor, 10);
    out.major = static_cast<int>(major);
    out.minor = static_cast<int>(minor);
    if (rest != nullptr) {
        *rest = end_minor != nullptr ? std::string(end_minor) : std::string();
    }
    return true;
}

std::vector<std::string> ClTools::split(const std::string& text) {
    std::vector<std::string> words;
    std::string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        } else {
            word.push_back(c);
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }
    return words;
}

bool ClTools::has_word(const std::string& list, const std::string& word) {
    const std::vector<std::string> words = split(list);
    return std::find(words.begin(), words.end(), word) != words.end();
}

bool ClTools::code_in(cl_int code, std::initializer_list<cl_int> accepted) {
    return std::find(accepted.begin(), accepted.end(), code) != accepted.end();
}

std::string ClTools::accepted_text(std::initializer_list<cl_int> accepted) {
    std::string out;
    for (const cl_int code : accepted) {
        out += (out.empty() ? "" : " or ") + error_name(code);
    }
    return out;
}

std::string ClTools::text_of(const std::vector<char>& blob) {
    std::string value(blob.begin(), blob.end());
    while (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
}

std::string ClTools::bytes_text(std::uint64_t bytes) {
    if (bytes >= (1ull << 20) && bytes % (1ull << 20) == 0) {
        return std::to_string(bytes >> 20) + " MiB";
    }
    return std::to_string(bytes) + " bytes";
}

}  // namespace lsw::suites
