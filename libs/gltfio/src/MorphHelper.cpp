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

#include "MorphHelper.h"

#include <filament/RenderableManager.h>
#include <filament/VertexBuffer.h>

#include "GltfEnums.h"

using namespace filament;
using namespace filament::math;
using namespace utils;

static const auto FREE_CALLBACK = [](void* mem, size_t, void*) { free(mem); };

namespace gltfio {

uint32_t computeBindingSize(const cgltf_accessor* accessor);
uint32_t computeBindingOffset(const cgltf_accessor* accessor);

MorphHelper::MorphHelper(FFilamentAsset* asset, FFilamentInstance* inst) : mAsset(asset),
        mInstance(inst) {
#warning TODO: support instances
    // Populate an inverse mapping between cgltf nodes and Filament Entities.
    for (auto pair : mAsset->mNodeMap) {
        mNodeMap[pair.second] = pair.first;
    }
}

MorphHelper::~MorphHelper() {
#warning TODO: destroy vertex buffers
}

void MorphHelper::applyWeights(Entity entity, float const* weights, size_t count) {
    auto renderableManager = &mAsset->mEngine->getRenderableManager();
    auto renderable = renderableManager->getInstance(entity);

    // We only allow for 256 weights because our set representation is a 4-tuple of bytes. Note that
    // 256 is much more than the glTF min spec of 4. In practice this number tends to be smallish.
    count = std::min(count, size_t(256));

    // Make a copy of the weights because we want to re-order them.
    auto& sorted = mPartiallySortedWeights;
    sorted.clear();
    sorted.insert(sorted.begin(), weights, weights + count);

    // Find the four highest weights in O(n) by doing a partial sort.
    if (count > 4) {
        std::nth_element(sorted.begin(), sorted.begin() + 4, sorted.end(), [](float a, float b) {
            return a > b;
        });
    }

    // Find the "primary indices" which are the indices of the four highest weights. This is O(n).
    ubyte4 primaryIndices = {0xff, 0xff, 0xff, 0xff};
    while (sorted.size() < 4) sorted.push_back(-1);
    const size_t primaryCount = std::min(size_t(4), count);
    for (size_t index = 0, primary = 0; index < count && primary < primaryCount; ++index) {
        const float w = weights[index];
        if (w > 0 && (w == sorted[0] || w == sorted[1] || w == sorted[2] || w == sorted[3])) {
            primaryIndices[primary++] = index;
        }
    }

    const MorphKey key = { entity, primaryIndices };
    auto iter = mMorphTable.find(key);
    if (iter == mMorphTable.end()) {
        MorphValue val = createMorphTableEntry(entity, primaryIndices);
        iter = mMorphTable.insert({key, val}).first;
        printf("prideout %d %d %d %d\n",
           primaryIndices[0], primaryIndices[1], primaryIndices[2], primaryIndices[3]);
    }

    const MorphValue& tableEntry = iter.value();
    for (size_t primIndex = 0; primIndex < tableEntry.size(); ++primIndex) {
        Primitive prim = tableEntry[primIndex];
        renderableManager->setGeometryAt(renderable, primIndex, prim.type, prim.vertices,
               prim.indices, 0, prim.indices->getIndexCount());
    }

    ubyte4 safeIndices = primaryIndices;
    for (int i = 0; i < 4; i++) if (safeIndices[i] == 0xff) safeIndices[i] = 0;
    const float4 highestWeights = {
        weights[safeIndices[0]],
        weights[safeIndices[1]],
        weights[safeIndices[2]],
        weights[safeIndices[3]]
    };

    //printf("prideout %d %d %d %d ... %g %g %g %g\n",
    //    primaryIndices[0], primaryIndices[1], primaryIndices[2], primaryIndices[3],
    //    highestWeights[0], highestWeights[1], highestWeights[2], highestWeights[3]);

    renderableManager->setMorphWeights(renderable, highestWeights);
}

MorphHelper::MorphValue MorphHelper::createMorphTableEntry(Entity entity, ubyte4 primaryIndices) {
    MorphValue result;
    const cgltf_node* node = mNodeMap[entity];
    const cgltf_mesh* mesh = node->mesh;
    const cgltf_primitive* prims = mesh->primitives;
    const cgltf_size prim_count = mesh->primitives_count;
    for (cgltf_size pi = 0; pi < prim_count; ++pi) {
        const cgltf_primitive& prim = prims[pi];
        RenderableManager::PrimitiveType prim_type;
        getPrimitiveType(prim.type, &prim_type);
        const auto& gltfioPrim = mAsset->mMeshCache.at(mesh)[pi];
        result.push_back({
            .vertices = createVertexBuffer(prim, gltfioPrim.uvmap, primaryIndices),
            .indices = gltfioPrim.indices,
            .type = prim_type,
        });
    }
    return result;
}

// This closely mimics AssetLoader::createPrimitive().
VertexBuffer* MorphHelper::createVertexBuffer(const cgltf_primitive& prim, const UvMap& uvmap,
        ubyte4 primaryIndices) {

    // This creates a copy because we don't know when the user will free the cgltf source data.
    // For non-morphed vertex buffers, we use a sharing mechanism to prevent copies, but here
    // we just want to keep it simple for now.
    auto createBufferDescriptor = [](const cgltf_accessor* accessor) {
        auto bufferData = (const uint8_t*) accessor->buffer_view->buffer->data;
        const uint8_t* data = computeBindingOffset(accessor) + bufferData;
        const uint32_t size = computeBindingSize(accessor);
        uint8_t* clone = (uint8_t*) malloc(size);
        memcpy(clone, data, size);
        return VertexBuffer::BufferDescriptor(clone, size, FREE_CALLBACK);
    };

    // TODO: this should be exposed by a static VertexBuffer method or constant
    static constexpr int kMaxBufferCount = 16;
    VertexBuffer::BufferDescriptor buffers[kMaxBufferCount] = {};

    VertexBuffer::Builder vbb;
    bool hasUv0 = false, hasUv1 = false, hasVertexColor = false, hasNormals = false;
    uint32_t vertexCount = 0;
    int slot = 0;
    for (cgltf_size aindex = 0; aindex < prim.attributes_count; aindex++) {
        const cgltf_attribute& attribute = prim.attributes[aindex];
        const int index = attribute.index;
        const cgltf_attribute_type atype = attribute.type;
        const cgltf_accessor* accessor = attribute.data;
        if (atype == cgltf_attribute_type_tangent) {
            continue;
        }
        if (atype == cgltf_attribute_type_normal) {
            vbb.attribute(VertexAttribute::TANGENTS, slot++, VertexBuffer::AttributeType::SHORT4);
            vbb.normalized(VertexAttribute::TANGENTS);
            hasNormals = true;
            continue;
        }
        if (atype == cgltf_attribute_type_color) {
            hasVertexColor = true;
        }
        VertexAttribute semantic;
        getVertexAttrType(atype, &semantic);
        if (atype == cgltf_attribute_type_texcoord) {
            if (index >= UvMapSize) {
                continue;
            }
            UvSet uvset = uvmap[index];
            switch (uvset) {
                case UV0:
                    semantic = VertexAttribute::UV0;
                    hasUv0 = true;
                    break;
                case UV1:
                    semantic = VertexAttribute::UV1;
                    hasUv1 = true;
                    break;
                case UNUSED:
                    if (!hasUv0 && getNumUvSets(uvmap) == 0) {
                        semantic = VertexAttribute::UV0;
                        hasUv0 = true;
                        break;
                    }
                    continue;
            }
        }
        vertexCount = accessor->count;
        VertexBuffer::AttributeType fatype;
        getElementType(accessor->type, accessor->component_type, &fatype);
        vbb.attribute(semantic, slot, fatype, 0, accessor->stride);
        vbb.normalized(semantic, accessor->normalized);
        buffers[slot] = createBufferDescriptor(accessor);
        slot++;
    }

    // If the model is lit but does not have normals, we'll need to generate flat normals.
    if (prim.material && !prim.material->unlit && !hasNormals) {
        vbb.attribute(VertexAttribute::TANGENTS, slot++, VertexBuffer::AttributeType::SHORT4);
        vbb.normalized(VertexAttribute::TANGENTS);
        cgltf_attribute_type atype = cgltf_attribute_type_normal;
    }

    constexpr int baseTangentsAttr = (int) VertexAttribute::MORPH_TANGENTS_0;
    constexpr int basePositionAttr = (int) VertexAttribute::MORPH_POSITION_0;

    int targetCount = 0;
    for (; targetCount < 4; ++targetCount) {
        cgltf_size targetIndex = primaryIndices[targetCount];
        if (targetIndex == 0xff) {
            break;
        }
        const cgltf_morph_target& morphTarget = prim.targets[targetIndex];
        for (cgltf_size aindex = 0; aindex < morphTarget.attributes_count; aindex++) {
            const cgltf_attribute& attribute = morphTarget.attributes[aindex];
            const cgltf_accessor* accessor = attribute.data;
            const cgltf_attribute_type atype = attribute.type;
            if (atype == cgltf_attribute_type_tangent) {
                continue;
            }
            if (atype == cgltf_attribute_type_normal) {
                VertexAttribute attr = (VertexAttribute) (baseTangentsAttr + targetCount);
                vbb.attribute(attr, slot++, VertexBuffer::AttributeType::SHORT4);
                vbb.normalized(attr);
                continue;
            }
            VertexBuffer::AttributeType fatype;
            getElementType(accessor->type, accessor->component_type, &fatype);
            VertexAttribute attr = (VertexAttribute) (basePositionAttr + targetCount);
            vbb.attribute(attr, slot, fatype, 0, accessor->stride);
            vbb.normalized(attr, accessor->normalized);
            buffers[slot] = createBufferDescriptor(accessor);
            slot++;
        }
    }

    vbb.vertexCount(vertexCount);

    // TODO: The following block is very inefficient because it assumes the worst case (ubershader)
    // and generates lots of dummy data. We should instead remember which texcoords are actually
    // required by the material provider.

    bool needsDummyData = false;
    if (!hasUv0) {
        needsDummyData = true;
        vbb.attribute(VertexAttribute::UV0, slot, VertexBuffer::AttributeType::USHORT2);
        vbb.normalized(VertexAttribute::UV0);
    }
    if (!hasUv1) {
        needsDummyData = true;
        vbb.attribute(VertexAttribute::UV1, slot, VertexBuffer::AttributeType::USHORT2);
        vbb.normalized(VertexAttribute::UV1);
    }
    if (!hasVertexColor) {
        needsDummyData = true;
        vbb.attribute(VertexAttribute::COLOR, slot, VertexBuffer::AttributeType::UBYTE4);
        vbb.normalized(VertexAttribute::COLOR);
    }

    const int bufferCount = needsDummyData ? slot + 1 : slot;

    vbb.bufferCount(bufferCount);
    VertexBuffer* vertices = vbb.build(*mAsset->mEngine);

#warning TODO: compute tangents etc

    for (int bufferIndex = 0; bufferIndex < kMaxBufferCount; ++bufferIndex) {
        if (buffers[bufferIndex].buffer == nullptr) {
            continue;
        }
        vertices->setBufferAt(*mAsset->mEngine, bufferIndex, std::move(buffers[bufferIndex]));
    }

    return vertices;
}

}  // namespace gltfio
