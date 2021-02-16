/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "VulkanBlitter.h"
#include "VulkanContext.h"
#include "VulkanHandles.h"

#include <utils/Panic.h>

#include "generated/vkshaders/vkshaders.h"

#define FILAMENT_VULKAN_CHECK_BLIT_FORMAT 0

using namespace bluevk;

namespace filament {
namespace backend {

VulkanBlitter::VulkanBlitter(VulkanContext& context, VulkanStagePool& stagePool, VulkanDisposer& disposer,
            VulkanBinder& binder) : mRenderPrimitive(new VulkanRenderPrimitive(context)), mContext(context),
            mStagePool(stagePool), mDisposer(disposer), mBinder(binder) {}

void VulkanBlitter::blitColor(VkCommandBuffer cmdBuffer, BlitArgs args) {
    const VulkanAttachment src = args.srcTarget->getColor(args.targetIndex);
    const VulkanAttachment dst = args.dstTarget->getColor(0);
    const VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;

#if FILAMENT_VULKAN_CHECK_BLIT_FORMAT
    const VkPhysicalDevice gpu = mContext.physicalDevice;
    VkFormatProperties info;
    vkGetPhysicalDeviceFormatProperties(gpu, src.format, &info);
    if (!ASSERT_POSTCONDITION_NON_FATAL(info.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT,
            "Source format is not blittable")) {
        return;
    }
    vkGetPhysicalDeviceFormatProperties(gpu, dst.format, &info);
    if (!ASSERT_POSTCONDITION_NON_FATAL(info.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT,
            "Destination format is not blittable")) {
        return;
    }
#endif

    blitFast(aspect, args.filter, args.srcTarget->getExtent(), src, dst, args.srcRectPair, args.dstRectPair,
            cmdBuffer);
}

void VulkanBlitter::blitDepth(VkCommandBuffer cmdBuffer, BlitArgs args) {
    const VulkanAttachment src = args.srcTarget->getDepth();
    const VulkanAttachment dst = args.dstTarget->getDepth();
    const VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;

#if FILAMENT_VULKAN_CHECK_BLIT_FORMAT
    const VkPhysicalDevice gpu = mContext.physicalDevice;
    VkFormatProperties info;
    vkGetPhysicalDeviceFormatProperties(gpu, src.format, &info);
    if (!ASSERT_POSTCONDITION_NON_FATAL(info.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT,
            "Depth format is not blittable")) {
        return;
    }
    vkGetPhysicalDeviceFormatProperties(gpu, dst.format, &info);
    if (!ASSERT_POSTCONDITION_NON_FATAL(info.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT,
            "Depth format is not blittable")) {
        return;
    }
#endif

    if (src.texture && src.texture->samples > 1 && dst.texture && dst.texture->samples == 1) {
        blitSlowDepth(aspect, args.filter, args.srcTarget->getExtent(), src, dst, args.srcRectPair,
            args.dstRectPair, cmdBuffer);
        return;
    }

    blitFast(aspect, args.filter, args.srcTarget->getExtent(), src, dst, args.srcRectPair,
            args.dstRectPair, cmdBuffer);
}

void VulkanBlitter::blitFast(VkImageAspectFlags aspect, VkFilter filter,
    const VkExtent2D srcExtent, VulkanAttachment src, VulkanAttachment dst,
    const VkOffset3D srcRect[2], const VkOffset3D dstRect[2], VkCommandBuffer cmdbuffer) {
    const VkImageBlit blitRegions[1] = {{
        .srcSubresource = { aspect, src.level, src.layer, 1 },
        .srcOffsets = { srcRect[0], srcRect[1] },
        .dstSubresource = { aspect, dst.level, dst.layer, 1 },
        .dstOffsets = { dstRect[0], dstRect[1] }
    }};

    const VkImageResolve resolveRegions[1] = {{
        .srcSubresource = { aspect, src.level, src.layer, 1 },
        .srcOffset = srcRect[0],
        .dstSubresource = { aspect, dst.level, dst.layer, 1 },
        .dstOffset = dstRect[0],
        .extent = { srcExtent.width, srcExtent.height, 1 }
    }};

    VulkanTexture::transitionImageLayout(cmdbuffer, src.image, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, src.level, 1, 1, aspect);

    VulkanTexture::transitionImageLayout(cmdbuffer, dst.image, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dst.level, 1, 1, aspect);

    if (src.texture && src.texture->samples > 1 && dst.texture && dst.texture->samples == 1) {
        assert(aspect != VK_IMAGE_ASPECT_DEPTH_BIT && "Resolve with depth is not yet supported.");
        vkCmdResolveImage(cmdbuffer, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, resolveRegions);
    } else {
        vkCmdBlitImage(cmdbuffer, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, blitRegions, filter);
    }

    if (src.texture) {
        VulkanTexture::transitionImageLayout(cmdbuffer, src.image, VK_IMAGE_LAYOUT_UNDEFINED,
                getTextureLayout(src.texture->usage), src.level, 1, 1, aspect);
    } else if  (!mContext.currentSurface->headlessQueue) {
        VulkanTexture::transitionImageLayout(cmdbuffer, src.image, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, src.level, 1, 1, aspect);
    }

    // Determine the desired texture layout for the destination while ensuring that the default
    // render target is supported, which has no associated texture.
    const VkImageLayout desiredLayout = dst.texture ? getTextureLayout(dst.texture->usage) :
            getSwapContext(mContext).attachment.layout;

    VulkanTexture::transitionImageLayout(cmdbuffer, dst.image, VK_IMAGE_LAYOUT_UNDEFINED,
            desiredLayout, dst.level, 1, 1, aspect);
}

void VulkanBlitter::shutdown() noexcept {
    if (mContext.device) {
        vkDestroyShaderModule(mContext.device, mVertexShader, VKALLOC);
        mVertexShader = nullptr;

        vkDestroyShaderModule(mContext.device, mFragmentShader, VKALLOC);
        mFragmentShader = nullptr;

        delete mTriangleVertices;
        mTriangleVertices = nullptr;

        delete mRenderPrimitive;
        mRenderPrimitive = nullptr;
    }
}

// If we created these shader modules in the constructor, the device might not be ready yet.
// It is easier to do lazy initialization, which can also improve load time.
void VulkanBlitter::lazyInit() noexcept {
    if (mVertexShader) {
        return;
    }
    assert(mContext.device);

    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    VkResult result;

    moduleInfo.codeSize = VKSHADERS_BLITDEPTHVS_SIZE;
    moduleInfo.pCode = (uint32_t*) VKSHADERS_BLITDEPTHVS_DATA;
    result = vkCreateShaderModule(mContext.device, &moduleInfo, VKALLOC, &mVertexShader);
    ASSERT_POSTCONDITION(result == VK_SUCCESS, "Unable to create vertex shader for blit.");

    moduleInfo.codeSize = VKSHADERS_BLITDEPTHFS_SIZE;
    moduleInfo.pCode = (uint32_t*) VKSHADERS_BLITDEPTHFS_DATA;
    result = vkCreateShaderModule(mContext.device, &moduleInfo, VKALLOC, &mFragmentShader);
    ASSERT_POSTCONDITION(result == VK_SUCCESS, "Unable to create fragment shader for blit.");

    static const float kTriangleVertices[] = {
        -1.0f, -1.0f,
        +1.0f, -1.0f,
        -1.0f, +1.0f,
        +1.0f, +1.0f,
    };

    mTriangleVertices = new VulkanBuffer(mContext, mStagePool, mDisposer, nullptr,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sizeof(kTriangleVertices));

    mTriangleVertices->loadFromCpu(kTriangleVertices, 0, sizeof(kTriangleVertices));

    mRenderPrimitive->primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    mRenderPrimitive->buffers.push_back(mTriangleVertices->getGpuBuffer());
    mRenderPrimitive->offsets.push_back(0);

    memset(&mRenderPrimitive->varray, 0, sizeof(mRenderPrimitive->varray));

    mRenderPrimitive->varray.attributes[0] = {
        .location = 0, // matches the GLSL layout specifier
        .binding = 0,  // matches the position within vkCmdBindVertexBuffers
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = 0
    };
    mRenderPrimitive->varray.buffers[0] = {
        .binding = 0,
        .stride = sizeof(float) * 2,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
}

void VulkanBlitter::blitSlowDepth(VkImageAspectFlags aspect, VkFilter filter,
        const VkExtent2D srcExtent, VulkanAttachment src, VulkanAttachment dst,
        const VkOffset3D srcRect[2], const VkOffset3D dstRect[2], VkCommandBuffer cmdbuffer) {
    lazyInit();

    puts("drawing a quad...");

    mContext.rasterState.depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };

    mContext.rasterState.multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = (VkSampleCountFlagBits) 1,
        .alphaToCoverageEnable = VK_FALSE,
    };

    mContext.rasterState.blending = {
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .alphaBlendOp =  VK_BLEND_OP_ADD,
        .colorWriteMask = 0xf,
    };

    VkPipelineRasterizationStateCreateInfo& vkraster = mContext.rasterState.rasterization;
    vkraster.cullMode = VK_CULL_MODE_NONE;
    vkraster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    vkraster.depthBiasEnable = VK_FALSE;

    mContext.rasterState.colorTargetCount = 0; // ?

    VulkanBinder::ProgramBundle shaderHandles = { mVertexShader, mFragmentShader };

    mBinder.bindProgramBundle(shaderHandles);
    mBinder.bindRasterState(mContext.rasterState);
    mBinder.bindPrimitiveTopology(mRenderPrimitive->primitiveTopology);
    mBinder.bindVertexArray(mRenderPrimitive->varray);

    VkDescriptorImageInfo samplers[VulkanBinder::SAMPLER_BINDING_COUNT] = {};
    mBinder.bindSamplers(samplers);

    VkRect2D scissor{
            .offset = {},
            .extent = { (uint32_t)right - x, (uint32_t)top - y }
    };


    vkCmdSetScissor(cmdbuffer, 0, 1, &scissor);

    // Bind new descriptor sets if they need to change.
    VkDescriptorSet descriptors[3];
    VkPipelineLayout pipelineLayout;
    if (mBinder.getOrCreateDescriptors(descriptors, &pipelineLayout)) {
        vkCmdBindDescriptorSets(cmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 3,
                descriptors, 0, nullptr);
    }

    // Bind the pipeline if it changed. This can happen, for example, if the raster state changed.
    // Creating a new pipeline is slow, so we should consider using pipeline cache objects.
    VkPipeline pipeline;
    if (mBinder.getOrCreatePipeline(&pipeline)) {
        vkCmdBindPipeline(cmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    }

    vkCmdBindVertexBuffers(cmdbuffer, 0, (uint32_t) mRenderPrimitive->buffers.size(),
            mRenderPrimitive->buffers.data(), mRenderPrimitive->offsets.data());

    vkCmdDrawIndexed(cmdbuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstId);

}

} // namespace filament
} // namespace backend
