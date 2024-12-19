#include "RenderAPI.h"
#include "PlatformBase.h"

#if SUPPORT_VULKAN

#include <string.h>
#include <map>
#include <vector>
#include <math.h>
#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

// This plugin does not link to the Vulkan loader, easier to support multiple APIs and systems that don't have Vulkan support
#define VK_NO_PROTOTYPES
#include "Unity/IUnityGraphicsVulkan.h"

#define UNITY_USED_VULKAN_API_FUNCTIONS(apply) \
    apply(vkCreateInstance); \
    apply(vkCmdBeginRenderPass); \
    apply(vkCreateBuffer); \
    apply(vkCreateImage); \
    apply(vkDestroyImage); \
    apply(vkCreateImageView); \
    apply(vkDestroyImageView); \
    apply(vkCreateSampler); \
    apply(vkDestroySampler); \
    apply(vkGetPhysicalDeviceMemoryProperties); \
    apply(vkGetPhysicalDeviceProperties); \
    apply(vkGetBufferMemoryRequirements); \
    apply(vkGetImageMemoryRequirements); \
    apply(vkMapMemory); \
    apply(vkBindBufferMemory); \
    apply(vkBindImageMemory); \
    apply(vkAllocateMemory); \
    apply(vkDestroyBuffer); \
    apply(vkFreeMemory); \
    apply(vkUnmapMemory); \
    apply(vkQueueSubmit); \
    apply(vkQueueWaitIdle); \
    apply(vkDeviceWaitIdle); \
    apply(vkCmdCopyBufferToImage); \
    apply(vkFlushMappedMemoryRanges); \
    apply(vkCreateDescriptorSetLayout); \
    apply(vkDestroyDescriptorSetLayout); \
    apply(vkCreateDescriptorPool); \
    apply(vkDestroyDescriptorPool); \
    apply(vkAllocateDescriptorSets); \
    apply(vkUpdateDescriptorSets); \
    apply(vkCreatePipelineLayout); \
    apply(vkCreateShaderModule); \
    apply(vkDestroyShaderModule); \
    apply(vkCreateGraphicsPipelines); \
    apply(vkCreateCommandPool); \
    apply(vkDestroyCommandPool); \
    apply(vkAllocateCommandBuffers); \
    apply(vkBeginCommandBuffer); \
    apply(vkEndCommandBuffer); \
    apply(vkFreeCommandBuffers); \
    apply(vkCmdPipelineBarrier); \
    apply(vkCmdBindPipeline); \
    apply(vkCmdDraw); \
    apply(vkCmdPushConstants); \
    apply(vkCmdBindDescriptorSets); \
    apply(vkCmdBindVertexBuffers); \
    apply(vkDestroyPipeline); \
    apply(vkDestroyPipelineLayout);
    
#define VULKAN_DEFINE_API_FUNCPTR(func) static PFN_##func func
VULKAN_DEFINE_API_FUNCPTR(vkGetInstanceProcAddr);
UNITY_USED_VULKAN_API_FUNCTIONS(VULKAN_DEFINE_API_FUNCPTR);
#undef VULKAN_DEFINE_API_FUNCPTR

static void LoadVulkanAPI(PFN_vkGetInstanceProcAddr getInstanceProcAddr, VkInstance instance)
{
    if (!vkGetInstanceProcAddr && getInstanceProcAddr)
        vkGetInstanceProcAddr = getInstanceProcAddr;

	if (!vkCreateInstance)
		vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");

#define LOAD_VULKAN_FUNC(fn) if (!fn) fn = (PFN_##fn)vkGetInstanceProcAddr(instance, #fn)
    UNITY_USED_VULKAN_API_FUNCTIONS(LOAD_VULKAN_FUNC);
#undef LOAD_VULKAN_FUNC
}

static VKAPI_ATTR void VKAPI_CALL Hook_vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents)
{
    // Change this to 'true' to override the clear color with green
	const bool allowOverrideClearColor = false;
    if (pRenderPassBegin->clearValueCount <= 16 && pRenderPassBegin->clearValueCount > 0 && allowOverrideClearColor)
    {
        VkClearValue clearValues[16] = {};
        memcpy(clearValues, pRenderPassBegin->pClearValues, pRenderPassBegin->clearValueCount * sizeof(VkClearValue));

        VkRenderPassBeginInfo patchedBeginInfo = *pRenderPassBegin;
        patchedBeginInfo.pClearValues = clearValues;
        for (unsigned int i = 0; i < pRenderPassBegin->clearValueCount - 1; ++i)
        {
            clearValues[i].color.float32[0] = 0.0;
            clearValues[i].color.float32[1] = 1.0;
            clearValues[i].color.float32[2] = 0.0;
            clearValues[i].color.float32[3] = 1.0;
        }
        vkCmdBeginRenderPass(commandBuffer, &patchedBeginInfo, contents);
    }
    else
    {
        vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL Hook_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance)
{
    vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    VkResult result = vkCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (result == VK_SUCCESS)
        LoadVulkanAPI(vkGetInstanceProcAddr, *pInstance);
 
    return result;
}

static int FindMemoryTypeIndex(VkPhysicalDeviceMemoryProperties const & physicalDeviceMemoryProperties, VkMemoryRequirements const & memoryRequirements, VkMemoryPropertyFlags memoryPropertyFlags)
{
    uint32_t memoryTypeBits = memoryRequirements.memoryTypeBits;

    // Search memtypes to find first index with those properties
    for (uint32_t memoryTypeIndex = 0; memoryTypeIndex < VK_MAX_MEMORY_TYPES; ++memoryTypeIndex)
    {
        if ((memoryTypeBits & 1) == 1)
        {
            // Type is available, does it match user properties?
            if ((physicalDeviceMemoryProperties.memoryTypes[memoryTypeIndex].propertyFlags & memoryPropertyFlags) == memoryPropertyFlags)
                return memoryTypeIndex;
        }
        memoryTypeBits >>= 1;
    }

    return -1;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL Hook_vkGetInstanceProcAddr(VkInstance device, const char* funcName)
{
    if (!funcName)
        return NULL;

#define INTERCEPT(fn) if (strcmp(funcName, #fn) == 0) return (PFN_vkVoidFunction)&Hook_##fn
    INTERCEPT(vkCreateInstance);
#undef INTERCEPT

    return NULL;
}

static PFN_vkGetInstanceProcAddr UNITY_INTERFACE_API InterceptVulkanInitialization(PFN_vkGetInstanceProcAddr getInstanceProcAddr, void*)
{
    vkGetInstanceProcAddr = getInstanceProcAddr;
    return Hook_vkGetInstanceProcAddr;
}

extern "C" void RenderAPI_Vulkan_OnPluginLoad(IUnityInterfaces* interfaces)
{
    if (IUnityGraphicsVulkanV2* vulkanInterface = interfaces->Get<IUnityGraphicsVulkanV2>())
        vulkanInterface->AddInterceptInitialization(InterceptVulkanInitialization, NULL, 0);
    else if (IUnityGraphicsVulkan* vulkanInterface = interfaces->Get<IUnityGraphicsVulkan>())
        vulkanInterface->InterceptInitialization(InterceptVulkanInitialization, NULL);
}

struct VulkanBuffer
{
    VkBuffer buffer;
    VkDeviceMemory deviceMemory;
    void* mapped;
    VkDeviceSize sizeInBytes;
    VkDeviceSize deviceMemorySize;
    VkMemoryPropertyFlags deviceMemoryFlags;
};

struct VulkanImage
{
	VkImage image;
	VkDeviceMemory deviceMemory;
	VkImageView imageView;
	VkSampler sampler;
	VkFormat format;
	uint32_t width;
	uint32_t height;
	uint32_t mipLevels;
	VkImageLayout imageLayout;
	VkImageAspectFlags aspectMask;
	VkMemoryPropertyFlags deviceMemoryFlags;
};

static VkPipelineLayout CreateTrianglePipelineLayout(VkDevice device, VkDescriptorSetLayout& descriptorSetLayout)
{
    VkPushConstantRange pushConstantRange;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 64; // single matrix
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;

    VkPipelineLayout pipelineLayout;
    return vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, NULL, &pipelineLayout) == VK_SUCCESS ? pipelineLayout : VK_NULL_HANDLE;
}

namespace Shader {
// Source of vertex shader (filename: shader.vert)
/*
#version 310 es
layout(location = 0) in highp vec3 vpos;
layout(location = 1) in highp vec4 vcol;
layout(location = 2) in highp vec2 vtex;
layout(location = 0) out highp vec4 color;
layout(location = 1) out highp vec2 ftex;
layout(push_constant) uniform PushConstants { mat4 matrix; };
void main() {
    gl_Position = matrix * vec4(vpos, 1.0);
    color = vcol;
    ftex = vtex;
}
*/

// Source of fragment shader (filename: shader.frag)
/*
#version 310 es
layout(location = 0) out highp vec4 fragColor;
layout(location = 1) in highp vec2 fragTexCoord;
layout(location = 0) in highp vec4 color;
layout(set = 1, binding = 1) uniform sampler2D texSampler;

void main() { fragColor = texture(texSampler, fragTexCoord); }
*/
// compiled to SPIR-V using:
// %VULKAN_SDK%\bin\glslc -mfmt=num shader.frag shader.vert -c

const uint32_t vertexShaderSpirv[] = {
0x07230203,0x00010000,0x000d000b,0x0000002d,
0x00000000,0x00020011,0x00000001,0x0006000b,
0x00000001,0x4c534c47,0x6474732e,0x3035342e,
0x00000000,0x0003000e,0x00000000,0x00000001,
0x000b000f,0x00000000,0x00000004,0x6e69616d,
0x00000000,0x0000000d,0x00000019,0x00000023,
0x00000025,0x00000029,0x0000002b,0x00030003,
0x00000002,0x000001c2,0x000a0004,0x475f4c47,
0x4c474f4f,0x70635f45,0x74735f70,0x5f656c79,
0x656e696c,0x7269645f,0x69746365,0x00006576,
0x00080004,0x475f4c47,0x4c474f4f,0x6e695f45,
0x64756c63,0x69645f65,0x74636572,0x00657669,
0x00040005,0x00000004,0x6e69616d,0x00000000,
0x00060005,0x0000000b,0x505f6c67,0x65567265,
0x78657472,0x00000000,0x00060006,0x0000000b,
0x00000000,0x505f6c67,0x7469736f,0x006e6f69,
0x00070006,0x0000000b,0x00000001,0x505f6c67,
0x746e696f,0x657a6953,0x00000000,0x00070006,
0x0000000b,0x00000002,0x435f6c67,0x4470696c,
0x61747369,0x0065636e,0x00070006,0x0000000b,
0x00000003,0x435f6c67,0x446c6c75,0x61747369,
0x0065636e,0x00030005,0x0000000d,0x00000000,
0x00060005,0x00000011,0x68737550,0x736e6f43,
0x746e6174,0x00000073,0x00050006,0x00000011,
0x00000000,0x7274616d,0x00007869,0x00030005,
0x00000013,0x00000000,0x00040005,0x00000019,
0x736f7076,0x00000000,0x00040005,0x00000023,
0x6f6c6f63,0x00000072,0x00040005,0x00000025,
0x6c6f6376,0x00000000,0x00040005,0x00000029,
0x78657466,0x00000000,0x00040005,0x0000002b,
0x78657476,0x00000000,0x00030047,0x0000000b,
0x00000002,0x00050048,0x0000000b,0x00000000,
0x0000000b,0x00000000,0x00050048,0x0000000b,
0x00000001,0x0000000b,0x00000001,0x00050048,
0x0000000b,0x00000002,0x0000000b,0x00000003,
0x00050048,0x0000000b,0x00000003,0x0000000b,
0x00000004,0x00030047,0x00000011,0x00000002,
0x00040048,0x00000011,0x00000000,0x00000005,
0x00050048,0x00000011,0x00000000,0x00000007,
0x00000010,0x00050048,0x00000011,0x00000000,
0x00000023,0x00000000,0x00040047,0x00000019,
0x0000001e,0x00000000,0x00040047,0x00000023,
0x0000001e,0x00000000,0x00040047,0x00000025,
0x0000001e,0x00000001,0x00040047,0x00000029,
0x0000001e,0x00000001,0x00040047,0x0000002b,
0x0000001e,0x00000002,0x00020013,0x00000002,
0x00030021,0x00000003,0x00000002,0x00030016,
0x00000006,0x00000020,0x00040017,0x00000007,
0x00000006,0x00000004,0x00040015,0x00000008,
0x00000020,0x00000000,0x0004002b,0x00000008,
0x00000009,0x00000001,0x0004001c,0x0000000a,
0x00000006,0x00000009,0x0006001e,0x0000000b,
0x00000007,0x00000006,0x0000000a,0x0000000a,
0x00040020,0x0000000c,0x00000003,0x0000000b,
0x0004003b,0x0000000c,0x0000000d,0x00000003,
0x00040015,0x0000000e,0x00000020,0x00000001,
0x0004002b,0x0000000e,0x0000000f,0x00000000,
0x00040018,0x00000010,0x00000007,0x00000004,
0x0003001e,0x00000011,0x00000010,0x00040020,
0x00000012,0x00000009,0x00000011,0x0004003b,
0x00000012,0x00000013,0x00000009,0x00040020,
0x00000014,0x00000009,0x00000010,0x00040017,
0x00000017,0x00000006,0x00000003,0x00040020,
0x00000018,0x00000001,0x00000017,0x0004003b,
0x00000018,0x00000019,0x00000001,0x0004002b,
0x00000006,0x0000001b,0x3f800000,0x00040020,
0x00000021,0x00000003,0x00000007,0x0004003b,
0x00000021,0x00000023,0x00000003,0x00040020,
0x00000024,0x00000001,0x00000007,0x0004003b,
0x00000024,0x00000025,0x00000001,0x00040017,
0x00000027,0x00000006,0x00000002,0x00040020,
0x00000028,0x00000003,0x00000027,0x0004003b,
0x00000028,0x00000029,0x00000003,0x00040020,
0x0000002a,0x00000001,0x00000027,0x0004003b,
0x0000002a,0x0000002b,0x00000001,0x00050036,
0x00000002,0x00000004,0x00000000,0x00000003,
0x000200f8,0x00000005,0x00050041,0x00000014,
0x00000015,0x00000013,0x0000000f,0x0004003d,
0x00000010,0x00000016,0x00000015,0x0004003d,
0x00000017,0x0000001a,0x00000019,0x00050051,
0x00000006,0x0000001c,0x0000001a,0x00000000,
0x00050051,0x00000006,0x0000001d,0x0000001a,
0x00000001,0x00050051,0x00000006,0x0000001e,
0x0000001a,0x00000002,0x00070050,0x00000007,
0x0000001f,0x0000001c,0x0000001d,0x0000001e,
0x0000001b,0x00050091,0x00000007,0x00000020,
0x00000016,0x0000001f,0x00050041,0x00000021,
0x00000022,0x0000000d,0x0000000f,0x0003003e,
0x00000022,0x00000020,0x0004003d,0x00000007,
0x00000026,0x00000025,0x0003003e,0x00000023,
0x00000026,0x0004003d,0x00000027,0x0000002c,
0x0000002b,0x0003003e,0x00000029,0x0000002c,
0x000100fd,0x00010038

};
const uint32_t fragmentShaderSpirv[] = {
0x07230203,0x00010000,0x000d000b,0x00000016,
0x00000000,0x00020011,0x00000001,0x0006000b,
0x00000001,0x4c534c47,0x6474732e,0x3035342e,
0x00000000,0x0003000e,0x00000000,0x00000001,
0x0008000f,0x00000004,0x00000004,0x6e69616d,
0x00000000,0x00000009,0x00000011,0x00000015,
0x00030010,0x00000004,0x00000007,0x00030003,
0x00000002,0x000001c2,0x000a0004,0x475f4c47,
0x4c474f4f,0x70635f45,0x74735f70,0x5f656c79,
0x656e696c,0x7269645f,0x69746365,0x00006576,
0x00080004,0x475f4c47,0x4c474f4f,0x6e695f45,
0x64756c63,0x69645f65,0x74636572,0x00657669,
0x00040005,0x00000004,0x6e69616d,0x00000000,
0x00050005,0x00000009,0x67617266,0x6f6c6f43,
0x00000072,0x00050005,0x0000000d,0x53786574,
0x6c706d61,0x00007265,0x00040005,0x00000011,
0x78657466,0x00000000,0x00040005,0x00000015,
0x6f6c6f63,0x00000072,0x00040047,0x00000009,
0x0000001e,0x00000000,0x00040047,0x0000000d,
0x00000021,0x00000000,0x00040047,0x0000000d,
0x00000022,0x00000000,0x00040047,0x00000011,
0x0000001e,0x00000001,0x00040047,0x00000015,
0x0000001e,0x00000000,0x00020013,0x00000002,
0x00030021,0x00000003,0x00000002,0x00030016,
0x00000006,0x00000020,0x00040017,0x00000007,
0x00000006,0x00000004,0x00040020,0x00000008,
0x00000003,0x00000007,0x0004003b,0x00000008,
0x00000009,0x00000003,0x00090019,0x0000000a,
0x00000006,0x00000001,0x00000000,0x00000000,
0x00000000,0x00000001,0x00000000,0x0003001b,
0x0000000b,0x0000000a,0x00040020,0x0000000c,
0x00000000,0x0000000b,0x0004003b,0x0000000c,
0x0000000d,0x00000000,0x00040017,0x0000000f,
0x00000006,0x00000002,0x00040020,0x00000010,
0x00000001,0x0000000f,0x0004003b,0x00000010,
0x00000011,0x00000001,0x00040020,0x00000014,
0x00000001,0x00000007,0x0004003b,0x00000014,
0x00000015,0x00000001,0x00050036,0x00000002,
0x00000004,0x00000000,0x00000003,0x000200f8,
0x00000005,0x0004003d,0x0000000b,0x0000000e,
0x0000000d,0x0004003d,0x0000000f,0x00000012,
0x00000011,0x00050057,0x00000007,0x00000013,
0x0000000e,0x00000012,0x0003003e,0x00000009,
0x00000013,0x000100fd,0x00010038

};
} // namespace Shader

static VkPipeline CreateTrianglePipeline(VkDevice device, VkPipelineLayout pipelineLayout, VkRenderPass renderPass, VkPipelineCache pipelineCache)
{
    if (pipelineLayout == VK_NULL_HANDLE)
        return VK_NULL_HANDLE; 
    if (device == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;
    if (renderPass == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    bool success = true;
    VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
     
    VkPipelineShaderStageCreateInfo shaderStages[2] = {};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].pName = "main";
    
    if (success)
    {
        VkShaderModuleCreateInfo moduleCreateInfo = {};
        moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleCreateInfo.codeSize = sizeof(Shader::vertexShaderSpirv);
        moduleCreateInfo.pCode = Shader::vertexShaderSpirv;
        success = vkCreateShaderModule(device, &moduleCreateInfo, NULL, &shaderStages[0].module) == VK_SUCCESS;
    }

    if (success)
    {
        VkShaderModuleCreateInfo moduleCreateInfo = {};
        moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleCreateInfo.codeSize = sizeof(Shader::fragmentShaderSpirv);
        moduleCreateInfo.pCode = Shader::fragmentShaderSpirv;
        success = vkCreateShaderModule(device, &moduleCreateInfo, NULL, &shaderStages[1].module) == VK_SUCCESS;
    }

    VkPipeline pipeline;
    if (success)
    {
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.layout = pipelineLayout;
        pipelineCreateInfo.renderPass = renderPass;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
        inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineRasterizationStateCreateInfo rasterizationState = {};
        rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationState.cullMode = VK_CULL_MODE_NONE;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationState.depthClampEnable = VK_FALSE;
        rasterizationState.rasterizerDiscardEnable = VK_FALSE;
        rasterizationState.depthBiasEnable = VK_FALSE;
        rasterizationState.lineWidth = 1.0f;

        VkPipelineColorBlendAttachmentState blendAttachmentState[1] = {};
        blendAttachmentState[0].colorWriteMask = 0xf;
        blendAttachmentState[0].blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo colorBlendState = {};
        colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendState.attachmentCount = 1;
        colorBlendState.pAttachments = blendAttachmentState;

        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        const VkDynamicState dynamicStateEnables[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState = {};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.pDynamicStates = dynamicStateEnables;
        dynamicState.dynamicStateCount = sizeof(dynamicStateEnables) / sizeof(*dynamicStateEnables);

        VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
        depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilState.depthTestEnable = VK_TRUE;
        depthStencilState.depthWriteEnable = VK_TRUE;
        depthStencilState.depthBoundsTestEnable = VK_FALSE;
        depthStencilState.stencilTestEnable = VK_FALSE;
        depthStencilState.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; // Unity/Vulkan uses reverse Z
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
        depthStencilState.front = depthStencilState.back;

        VkPipelineMultisampleStateCreateInfo multisampleState = {};
        multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampleState.pSampleMask = NULL;

        // Vertex:
        // float3 vpos;
        // float2 vtex;
        // byte4 vcol;
        VkVertexInputBindingDescription vertexInputBinding = {};
        vertexInputBinding.binding = 0;
        vertexInputBinding.stride = 24; 
        vertexInputBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription vertexInputAttributes[3];
        // vpos
        vertexInputAttributes[0].binding = 0;
        vertexInputAttributes[0].location = 0;
        vertexInputAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertexInputAttributes[0].offset = 0;
        // vcol
        vertexInputAttributes[1].binding = 0;
        vertexInputAttributes[1].location = 1;
        vertexInputAttributes[1].format = VK_FORMAT_R8G8B8A8_UNORM;
        vertexInputAttributes[1].offset = 12;
        // vtex
        vertexInputAttributes[2].binding = 0;
        vertexInputAttributes[2].location = 2;
        vertexInputAttributes[2].format = VK_FORMAT_R32G32_SFLOAT;
        vertexInputAttributes[2].offset = 16;


        VkPipelineVertexInputStateCreateInfo vertexInputState = {};
        vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputState.vertexBindingDescriptionCount = 1;
        vertexInputState.pVertexBindingDescriptions = &vertexInputBinding;
        vertexInputState.vertexAttributeDescriptionCount = 2;
        vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes;

        pipelineCreateInfo.stageCount = sizeof(shaderStages) / sizeof(*shaderStages);
        pipelineCreateInfo.pStages = shaderStages;
        pipelineCreateInfo.pVertexInputState = &vertexInputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;

        success = vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, NULL, &pipeline) == VK_SUCCESS;
    } 

    if (shaderStages[0].module != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, shaderStages[0].module, NULL);
    if (shaderStages[1].module != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, shaderStages[1].module, NULL);

    return success ? pipeline : VK_NULL_HANDLE;
}


class RenderAPI_Vulkan : public RenderAPI
{
public:
    RenderAPI_Vulkan();
    virtual ~RenderAPI_Vulkan() { }

    virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces);
    virtual bool GetUsesReverseZ() { return true; }
    virtual void DrawSimpleTriangles(const float worldMatrix[16], int triangleCount, const void* verticesFloat3Byte4);
    virtual void* BeginModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int* outRowPitch);
    virtual void EndModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int rowPitch, void* dataPtr);
    virtual void* BeginModifyVertexBuffer(void* bufferHandle, size_t* outBufferSize);
    virtual void EndModifyVertexBuffer(void* bufferHandle);
    virtual void drawToRenderTexture();
    virtual void drawToPluginTexture();
    virtual void* getNativeTexture();

private:
    typedef std::vector<VulkanBuffer> VulkanBuffers;
    typedef std::map<unsigned long long, VulkanBuffers> DeleteQueue;

private:
    bool CreateVulkanBuffer(size_t bytes, VulkanBuffer* buffer, VkBufferUsageFlags usage);
    void ImmediateDestroyVulkanBuffer(const VulkanBuffer& buffer);
	void ImmediateDestroyVulkanImage(const VulkanImage& image);
    void SafeDestroy(unsigned long long frameNumber, const VulkanBuffer& buffer);
    void GarbageCollect(bool force = false);
    void TransitionLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
    void CreateVulkanImage(int texWidth, int texHeight, VkFormat format, VkImage& image, VkDeviceMemory& imageMemory);
    void CreateCommandPool();
	void CreateTextureImage(const char* filename, VulkanImage& image);
    void CreateTextureImageView(VulkanImage& image);
    void CreateTextureSampler(VulkanImage& image);
    void CreateDescriptorSetLayout();
    void CreateDescriptorPool();
    void CreateDescriptorSets();
    void CopyFromBuffer(VulkanBuffer& buffer, VkImage& image, uint32_t width, uint32_t height);
    void InitResources();


private:
    IUnityGraphicsVulkan* m_UnityVulkan;
    UnityVulkanInstance m_Instance;
    VulkanBuffer m_TextureStagingBuffer;
    VulkanBuffer m_VertexStagingBuffer;
    std::map<unsigned long long, VulkanBuffers> m_DeleteQueue;
    VkPipelineLayout m_TrianglePipelineLayout;
    VkPipeline m_TrianglePipeline;
    VkRenderPass m_TrianglePipelineRenderPass;

    VkCommandPool m_CommandPool;
    VulkanImage m_Image1;

    VkDescriptorSetLayout m_DescriptorSetLayout;
    VkDescriptorPool m_DescriptorPool;
    std::vector<VkDescriptorSet> m_DescriptorSets;
};


RenderAPI* CreateRenderAPI_Vulkan()
{
    return new RenderAPI_Vulkan();
}

RenderAPI_Vulkan::RenderAPI_Vulkan()
    : m_UnityVulkan(NULL)
    , m_TextureStagingBuffer()
    , m_VertexStagingBuffer()
    , m_TrianglePipelineLayout(VK_NULL_HANDLE)
    , m_TrianglePipeline(VK_NULL_HANDLE)
    , m_TrianglePipelineRenderPass(VK_NULL_HANDLE)
    , m_CommandPool(VK_NULL_HANDLE)
    , m_DescriptorSetLayout(VK_NULL_HANDLE)
    , m_DescriptorPool(VK_NULL_HANDLE)
    , m_Image1()
{
}

void RenderAPI_Vulkan::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
    switch (type)
    {
    case kUnityGfxDeviceEventInitialize:
        m_UnityVulkan = interfaces->Get<IUnityGraphicsVulkan>();
        m_Instance = m_UnityVulkan->Instance();

        // Make sure Vulkan API functions are loaded
        LoadVulkanAPI(m_Instance.getInstanceProcAddr, m_Instance.instance);

        // Create Vulkan resources
        InitResources();

        UnityVulkanPluginEventConfig config_1;
        config_1.graphicsQueueAccess = kUnityVulkanGraphicsQueueAccess_DontCare;
        config_1.renderPassPrecondition = kUnityVulkanRenderPass_EnsureInside;
        config_1.flags = kUnityVulkanEventConfigFlag_EnsurePreviousFrameSubmission | kUnityVulkanEventConfigFlag_ModifiesCommandBuffersState;
        m_UnityVulkan->ConfigureEvent(1, &config_1);  // Event 1 corresponds to OnRender

        // alternative way to intercept API
        m_UnityVulkan->InterceptVulkanAPI("vkCmdBeginRenderPass", (PFN_vkVoidFunction)Hook_vkCmdBeginRenderPass);
        
        break;
    case kUnityGfxDeviceEventShutdown:

        if (m_Instance.device != VK_NULL_HANDLE)
        {
            GarbageCollect(true);
            if (m_TrianglePipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(m_Instance.device, m_TrianglePipeline, NULL);
                m_TrianglePipeline = VK_NULL_HANDLE;
            }
            if (m_TrianglePipelineLayout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(m_Instance.device, m_TrianglePipelineLayout, NULL);
                m_TrianglePipelineLayout = VK_NULL_HANDLE;
            }
			ImmediateDestroyVulkanImage(m_Image1);
            if (m_DescriptorPool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(m_Instance.device, m_DescriptorPool, NULL);
                m_DescriptorPool = VK_NULL_HANDLE;
            }
            if (m_DescriptorSetLayout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(m_Instance.device, m_DescriptorSetLayout, NULL);
                m_DescriptorSetLayout = VK_NULL_HANDLE;
            }
            if (m_CommandPool != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(m_Instance.device, m_CommandPool, NULL);
            }
        }

        m_UnityVulkan = NULL;
        m_TrianglePipelineRenderPass = VK_NULL_HANDLE;
        m_Instance = UnityVulkanInstance();

        break;
    }
}

void RenderAPI_Vulkan::drawToPluginTexture()
{
    return;
}

void RenderAPI_Vulkan::drawToRenderTexture()
{
    return;
}

void* RenderAPI_Vulkan::getNativeTexture()
{
    return &m_Image1.image;
}


void RenderAPI_Vulkan::InitResources()
{
    CreateCommandPool();
    CreateTextureImage("C:\\Users\\wayne\\OneDrive\\Pictures\\texture.jpg", m_Image1);
    CreateDescriptorSetLayout();
    CreateDescriptorPool();
    CreateDescriptorSets();
}

void RenderAPI_Vulkan::CreateDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;

    if (vkCreateDescriptorSetLayout(m_Instance.device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

void RenderAPI_Vulkan::CreateDescriptorPool()
{
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 5;

    if (vkCreateDescriptorPool(m_Instance.device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }
}

void RenderAPI_Vulkan::CreateDescriptorSets()
{
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_DescriptorSetLayout;

    m_DescriptorSets.resize(1);
    if (vkAllocateDescriptorSets(m_Instance.device, &allocInfo, m_DescriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate descriptor sets!");

    // Update descriptor set
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = m_Image1.imageView;
    imageInfo.sampler = m_Image1.sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_DescriptorSets[0];
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_Instance.device, 1, &descriptorWrite, 0, nullptr);
}

void RenderAPI_Vulkan::CreateTextureImageView(VulkanImage& image)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = image.format;
    viewInfo.subresourceRange.aspectMask = image.aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Instance.device, &viewInfo, nullptr, &image.imageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view.");
    }
}

void RenderAPI_Vulkan::CreateTextureSampler(VulkanImage& image)
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;

    //VkPhysicalDeviceProperties properties{};
    //vkGetPhysicalDeviceProperties(m_Instance.physicalDevice, &properties);

    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(m_Instance.device, &samplerInfo, nullptr, &image.sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture sampler!");
    }
}

void RenderAPI_Vulkan::CreateCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_Instance.queueFamilyIndex;

    if (vkCreateCommandPool(m_Instance.device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Command Pool");
    }
}

void RenderAPI_Vulkan::CreateVulkanImage(int texWidth, int texHeight, VkFormat format, VkImage& image, VkDeviceMemory& imageMemory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(texWidth);
    imageInfo.extent.height = static_cast<uint32_t>(texHeight);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;

    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0;

    if (vkCreateImage(m_Instance.device, &imageInfo, nullptr, &image) != VK_SUCCESS)
        throw std::runtime_error("Failed to create vulkan image.");

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Instance.device, image, &memRequirements);

    VkPhysicalDeviceMemoryProperties physicalDeviceProperties;
    vkGetPhysicalDeviceMemoryProperties(m_Instance.physicalDevice, &physicalDeviceProperties);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    const int memoryTypeIndex = FindMemoryTypeIndex(physicalDeviceProperties, memRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryTypeIndex < 0)
    {
        throw std::runtime_error("Cannot find memory type.");
    }

    allocInfo.memoryTypeIndex = memoryTypeIndex;

    if (vkAllocateMemory(m_Instance.device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image memory.");
    }

    vkBindImageMemory(m_Instance.device, image, imageMemory, 0);
}

void RenderAPI_Vulkan::TransitionLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    auto hasStencilComponent = [](VkFormat format) {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
    };

    // Use an image memory barrier (type of pipeline barrier) to transition image layout
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;

    if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

        if (hasStencilComponent(format)) {
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }
    else {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else {
        throw std::invalid_argument("unsupported layout transition!");
    }

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_Instance.device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(m_Instance.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit");
    if (vkQueueWaitIdle(m_Instance.graphicsQueue) != VK_SUCCESS)
        throw std::runtime_error("Failed to wait");
    
    vkFreeCommandBuffers(m_Instance.device, m_CommandPool, 1, &commandBuffer);
}

void RenderAPI_Vulkan::CopyFromBuffer(VulkanBuffer& buffer, VkImage& image, uint32_t width, uint32_t height) {
    // Specify which part of the buffer is going to be copied to which part of the image
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_Instance.device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    vkCmdCopyBufferToImage(commandBuffer, buffer.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    // NOTE: Should we use transferQueue here or it doesn't matter?
    if (vkQueueSubmit(m_Instance.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit");
    if (vkQueueWaitIdle(m_Instance.graphicsQueue) != VK_SUCCESS)
        throw std::runtime_error("Failed to wait");
    vkFreeCommandBuffers(m_Instance.device, m_CommandPool, 1, &commandBuffer);
}


void RenderAPI_Vulkan::CreateTextureImage(const char* filename, VulkanImage& image)
{
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filename, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        throw std::runtime_error(stbi_failure_reason());
    }

    image.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image.format = VK_FORMAT_R8G8B8A8_SRGB;
    image.width = texWidth;
    image.height = texHeight;

    VkDeviceSize imageSize = texWidth * texHeight * 4;
    VulkanBuffer stagingBuffer;
    if (!CreateVulkanBuffer(imageSize, &stagingBuffer, VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
        throw std::runtime_error("Failed to create staging buffer");

    memcpy(stagingBuffer.mapped, pixels, static_cast<size_t>(stagingBuffer.sizeInBytes));

    stbi_image_free(pixels);

    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;

    CreateVulkanImage(texWidth, texHeight, format, image.image, image.deviceMemory);

    // Change layout for transferring
    TransitionLayout(image.image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    
    // Copy from Buffer
    CopyFromBuffer(stagingBuffer, image.image, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    
    // Change layout for shader access
    TransitionLayout(image.image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Clean up
    ImmediateDestroyVulkanBuffer(stagingBuffer);

    CreateTextureImageView(image);
    CreateTextureSampler(image);
}


bool RenderAPI_Vulkan::CreateVulkanBuffer(size_t sizeInBytes, VulkanBuffer* buffer, VkBufferUsageFlags usage)
{
    if (sizeInBytes == 0)
        return false;

    VkBufferCreateInfo bufferCreateInfo;
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.pNext = NULL;
    bufferCreateInfo.pQueueFamilyIndices = &m_Instance.queueFamilyIndex;
    bufferCreateInfo.queueFamilyIndexCount = 1;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.usage = usage;
    bufferCreateInfo.flags = 0;
    bufferCreateInfo.size = sizeInBytes;

    *buffer = VulkanBuffer();

    if (vkCreateBuffer(m_Instance.device, &bufferCreateInfo, NULL, &buffer->buffer) != VK_SUCCESS)
        return false;

    VkPhysicalDeviceMemoryProperties physicalDeviceProperties;
    vkGetPhysicalDeviceMemoryProperties(m_Instance.physicalDevice, &physicalDeviceProperties);

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(m_Instance.device, buffer->buffer, &memoryRequirements);

    const int memoryTypeIndex = FindMemoryTypeIndex(physicalDeviceProperties, memoryRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (memoryTypeIndex < 0)
    {
        ImmediateDestroyVulkanBuffer(*buffer);
        return false;
    }

    VkMemoryAllocateInfo memoryAllocateInfo;
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.pNext = NULL;
    memoryAllocateInfo.memoryTypeIndex = memoryTypeIndex;
    memoryAllocateInfo.allocationSize = memoryRequirements.size;

    if (vkAllocateMemory(m_Instance.device, &memoryAllocateInfo, NULL, &buffer->deviceMemory) != VK_SUCCESS)
    {
        ImmediateDestroyVulkanBuffer(*buffer);
        return false;
    }

    if (vkMapMemory(m_Instance.device, buffer->deviceMemory, 0, VK_WHOLE_SIZE, 0, &buffer->mapped) != VK_SUCCESS)
    {
        ImmediateDestroyVulkanBuffer(*buffer);
        return false;
    }

    if (vkBindBufferMemory(m_Instance.device, buffer->buffer, buffer->deviceMemory, 0) != VK_SUCCESS)
    {
        ImmediateDestroyVulkanBuffer(*buffer);
        return false;
    }

    buffer->sizeInBytes = sizeInBytes;
    buffer->deviceMemoryFlags = physicalDeviceProperties.memoryTypes[memoryTypeIndex].propertyFlags;
    buffer->deviceMemorySize = memoryAllocateInfo.allocationSize;

    return true;
}

void RenderAPI_Vulkan::ImmediateDestroyVulkanImage(const VulkanImage& image)
{
	if (image.image != VK_NULL_HANDLE)
		vkDestroyImage(m_Instance.device, image.image, NULL);
	if (image.deviceMemory != VK_NULL_HANDLE)
		vkFreeMemory(m_Instance.device, image.deviceMemory, NULL);
	if (image.imageView != VK_NULL_HANDLE)
		vkDestroyImageView(m_Instance.device, image.imageView, NULL);
	if (image.sampler != VK_NULL_HANDLE)
		vkDestroySampler(m_Instance.device, image.sampler, NULL);
}

void RenderAPI_Vulkan::ImmediateDestroyVulkanBuffer(const VulkanBuffer& buffer)
{
    if (buffer.buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(m_Instance.device, buffer.buffer, NULL);

    if (buffer.mapped && buffer.deviceMemory != VK_NULL_HANDLE)
        vkUnmapMemory(m_Instance.device, buffer.deviceMemory);

    if (buffer.deviceMemory != VK_NULL_HANDLE)
        vkFreeMemory(m_Instance.device, buffer.deviceMemory, NULL);
}


void RenderAPI_Vulkan::SafeDestroy(unsigned long long frameNumber, const VulkanBuffer& buffer)
{
    m_DeleteQueue[frameNumber].push_back(buffer);
}

void RenderAPI_Vulkan::GarbageCollect(bool force /*= false*/)
{
    UnityVulkanRecordingState recordingState;
    if (force)
        recordingState.safeFrameNumber = ~0ull;
    else
        if (!m_UnityVulkan->CommandRecordingState(&recordingState, kUnityVulkanGraphicsQueueAccess_DontCare))
            return;

    DeleteQueue::iterator it = m_DeleteQueue.begin();
    while (it != m_DeleteQueue.end())
    {
        if (it->first <= recordingState.safeFrameNumber)
        {
            for (size_t i = 0; i < it->second.size(); ++i)
                ImmediateDestroyVulkanBuffer(it->second[i]);
            m_DeleteQueue.erase(it++);
        }
        else
            ++it;
    }
}

void RenderAPI_Vulkan::DrawSimpleTriangles(const float worldMatrix[16], int triangleCount, const void* verticesFloat3Byte4)
{
     // not needed, we already configured the event to be inside a render pass
     //   m_UnityVulkan->EnsureInsideRenderPass();

    UnityVulkanRecordingState recordingState;
    if (!m_UnityVulkan->CommandRecordingState(&recordingState, kUnityVulkanGraphicsQueueAccess_DontCare))
        return;

    // Unity does not destroy render passes, so this is safe regarding ABA-problem
    if (recordingState.renderPass != m_TrianglePipelineRenderPass)
    {
        if (m_TrianglePipelineLayout == VK_NULL_HANDLE)
            m_TrianglePipelineLayout = CreateTrianglePipelineLayout(m_Instance.device, m_DescriptorSetLayout);

        m_TrianglePipeline = CreateTrianglePipeline(m_Instance.device, m_TrianglePipelineLayout, recordingState.renderPass, VK_NULL_HANDLE);
		m_TrianglePipelineRenderPass = recordingState.renderPass;
    }

    if (m_TrianglePipeline != VK_NULL_HANDLE && m_TrianglePipelineLayout != VK_NULL_HANDLE)
    {
        VulkanBuffer buffer;
        if (!CreateVulkanBuffer(24 * 3 * triangleCount, &buffer, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
            return;

        memcpy(buffer.mapped, verticesFloat3Byte4, static_cast<size_t>(buffer.sizeInBytes));
        if (!(buffer.deviceMemoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        {
            VkMappedMemoryRange range;
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.pNext = NULL;
            range.memory = buffer.deviceMemory;
            range.offset = 0;
            range.size = buffer.deviceMemorySize;
            vkFlushMappedMemoryRanges(m_Instance.device, 1, &range);
        }

        // Record a series of commands that will be submitted to the queue
        const VkDeviceSize offset = 0;
        vkCmdBindPipeline(recordingState.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_TrianglePipeline);
        vkCmdBindVertexBuffers(recordingState.commandBuffer, 0, 1, &buffer.buffer, &offset);
        vkCmdPushConstants(recordingState.commandBuffer, m_TrianglePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, (const void*)worldMatrix);
        vkCmdBindDescriptorSets(recordingState.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_TrianglePipelineLayout, 0, 1, m_DescriptorSets.data(), 0, nullptr);
        vkCmdDraw(recordingState.commandBuffer, triangleCount * 3, 1, 0, 0);

        SafeDestroy(recordingState.currentFrameNumber, buffer);
    }

    GarbageCollect();
}

void* RenderAPI_Vulkan::BeginModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int* outRowPitch)
{
    *outRowPitch = textureWidth * 4;
    const size_t stagingBufferSizeRequirements = *outRowPitch * textureHeight;

    UnityVulkanRecordingState recordingState;
    if (!m_UnityVulkan->CommandRecordingState(&recordingState, kUnityVulkanGraphicsQueueAccess_DontCare))
        return NULL;

    SafeDestroy(recordingState.currentFrameNumber, m_TextureStagingBuffer);
    m_TextureStagingBuffer = VulkanBuffer();
    if (!CreateVulkanBuffer(stagingBufferSizeRequirements, &m_TextureStagingBuffer, VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
        return NULL;

    return m_TextureStagingBuffer.mapped;
}

void RenderAPI_Vulkan::EndModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int rowPitch, void* dataPtr)
{
    // cannot do resource uploads inside renderpass
    m_UnityVulkan->EnsureOutsideRenderPass();

    UnityVulkanImage image;
    if (!m_UnityVulkan->AccessTexture(textureHandle, UnityVulkanWholeImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, kUnityVulkanResourceAccess_PipelineBarrier, &image))
        return;

    UnityVulkanRecordingState recordingState;
    if (!m_UnityVulkan->CommandRecordingState(&recordingState, kUnityVulkanGraphicsQueueAccess_DontCare))
        return;

    VkBufferImageCopy region;
    region.bufferImageHeight = 0;
    region.bufferRowLength = 0;
    region.bufferOffset = 0;
    region.imageOffset.x = 0;
    region.imageOffset.y = 0;
    region.imageOffset.z = 0;
    region.imageExtent.width = textureWidth;
    region.imageExtent.height = textureHeight;
    region.imageExtent.depth = 1;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageSubresource.mipLevel = 0;
    vkCmdCopyBufferToImage(recordingState.commandBuffer, m_TextureStagingBuffer.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void* RenderAPI_Vulkan::BeginModifyVertexBuffer(void* bufferHandle, size_t* outBufferSize)
{
    UnityVulkanRecordingState recordingState;
    if (!m_UnityVulkan->CommandRecordingState(&recordingState, kUnityVulkanGraphicsQueueAccess_DontCare))
        return NULL;

    UnityVulkanBuffer bufferInfo;
    if (!m_UnityVulkan->AccessBuffer(bufferHandle, 0, 0, kUnityVulkanResourceAccess_ObserveOnly, &bufferInfo))
        return NULL;

    *outBufferSize = bufferInfo.sizeInBytes;

    if (!bufferInfo.memory.mapped)
        return NULL;

    // We don't want to start modifying a resource that might still be used by the GPU,
    // so we can use kUnityVulkanResourceAccess_Recreate to recreate it while still keeping the old one alive if it's in use.
    UnityVulkanBuffer recreatedBuffer;
    if (!m_UnityVulkan->AccessBuffer(bufferHandle, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT, kUnityVulkanResourceAccess_Recreate, &recreatedBuffer))
        return NULL;

    // We don't care about the previous contents of this vertex buffer so we can return the mapped pointer to the new resource memory 
    return recreatedBuffer.memory.mapped;
}

void RenderAPI_Vulkan::EndModifyVertexBuffer(void* bufferHandle)
{
    // cannot do resource uploads inside renderpass, but we know that the texture modification is done first and that already ends the renderpass
    // m_UnityVulkan->EnsureOutsideRenderPass(); 

    UnityVulkanBuffer buffer;
    if (!m_UnityVulkan->AccessBuffer(bufferHandle, 0, 0, kUnityVulkanResourceAccess_ObserveOnly, &buffer))
        return;

    if (!(buffer.memory.flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    {
        VkMappedMemoryRange range;
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.pNext = NULL;
        range.memory = buffer.memory.memory;
        range.offset = buffer.memory.offset; // size and offset also must be multiple of nonCoherentAtomSize
        range.size = buffer.memory.size;
        vkFlushMappedMemoryRanges(m_Instance.device, 1, &range);
    }
}

#endif // #if SUPPORT_VULKAN
