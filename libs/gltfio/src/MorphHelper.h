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

#include "FFilamentAsset.h"
#include "FFilamentInstance.h"

#include <math/vec4.h>

#include <utils/Hash.h>

#include <tsl/robin_map.h>

#include <vector>

struct cgltf_node;
struct cgltf_primitive;

namespace gltfio {

/**
 * Internal class that partitions lists of morph weights and maintains a cache of VertexBuffer
 * objects for each partition.
 *
 * MorphHelper allows Filament to fully support meshes with many morph targets, as long as no more
 * than 4 are ever used simultaneously. If more than 4 are used simultaneously, MorphHelpers falls
 * back to a reasonable compromise by picking the 4 most influential weight values.
 *
 * Animator has ownership over a single instance of MorphHelper, thus it is 1:1 with FilamentAsset.
 */
class MorphHelper {
public:
    MorphHelper(FFilamentAsset* asset, FFilamentInstance* inst);
    ~MorphHelper();
    void applyWeights(utils::Entity targetEntity, float const* weights, size_t count);

private:
    using ubyte4 = filament::math::ubyte4;
    using Entity = utils::Entity;
    using VertexBuffer = filament::VertexBuffer;

    struct MorphKey {
        Entity targetEntity;
        ubyte4 primaryIndices;
    };

    struct Primitive {
        filament::VertexBuffer* vertices;
        filament::IndexBuffer* indices;
        filament::RenderableManager::PrimitiveType type;
    };

    using MorphValue = std::vector<Primitive>;
    using MorphKeyHashFn = utils::hash::MurmurHashFn<MorphKey>;

    struct MorphKeyEqualFn {
        bool operator()(const MorphKey& k1, const MorphKey& k2) const {
            return k1.targetEntity == k2.targetEntity &&
                    k1.primaryIndices == k2.primaryIndices;
        }
    };

    MorphValue createMorphTableEntry(Entity targetEntity, ubyte4 primaryIndices);
    VertexBuffer* createVertexBuffer(const cgltf_primitive& prim, const UvMap& uvmap,
            ubyte4 primaryIndices);

    std::vector<float> mPartiallySortedWeights;
    tsl::robin_map<MorphKey, MorphValue, MorphKeyHashFn, MorphKeyEqualFn> mMorphTable;
    tsl::robin_map<Entity, const cgltf_node*> mNodeMap;
    const FFilamentAsset* mAsset;
    const FFilamentInstance* mInstance;
};

} // namespace gltfio
