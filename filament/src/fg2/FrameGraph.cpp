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

#include "fg2/FrameGraph.h"
#include "fg2/details/PassNode.h"
#include "fg2/details/ResourceNode.h"
#include "fg2/details/DependencyGraph.h"

#include "details/Engine.h"

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

namespace filament::fg2 {

FrameGraph::Builder::Builder(FrameGraph& fg, PassNode& pass) noexcept
        : mFrameGraph(fg), mPass(pass) {
}

FrameGraph::Builder::~Builder() noexcept = default;

void FrameGraph::Builder::sideEffect() noexcept {
    mPass.makeTarget();
}

const char* FrameGraph::Builder::getName(FrameGraphHandle handle) const noexcept {
    return mFrameGraph.getResource(handle)->name;
}

RenderTarget FrameGraph::Builder::useAsRenderTarget(const char* name,
        RenderTarget::Descriptor const& desc) noexcept {
    // it's safe here to cast to RenderPassNode because we can't be here for a PresentPassNode
    // also only RenderPassNodes have the concept of render targets.
    return static_cast<RenderPassNode&>(mPass).declareRenderTarget(mFrameGraph, *this, name, desc);
}

uint32_t FrameGraph::Builder::useAsRenderTarget(FrameGraphId<Texture>* color) noexcept {
    assert(color);
    auto[attachments, id] = useAsRenderTarget(getName(*color),
            { .attachments = { .color = { *color }}});
    *color = attachments.color[0];
    return id;
}

uint32_t FrameGraph::Builder::useAsRenderTarget(FrameGraphId<Texture>* color,
        FrameGraphId<Texture>* depth) noexcept {
    assert(color || depth);
    RenderTarget::Descriptor desc;
    if (color) {
        desc.attachments.color[0] = *color;
    }
    if (depth) {
        desc.attachments.depth = *depth;
    }
    auto[attachments, id] = useAsRenderTarget(getName(color ? *color : *depth), desc);
    if (color) {
        *color = attachments.color[0];
    }
    if (depth) {
        *depth = attachments.depth;
    }
    return id;
}

// ------------------------------------------------------------------------------------------------

FrameGraph::FrameGraph(ResourceAllocatorInterface& resourceAllocator)
        : mResourceAllocator(resourceAllocator),
          mArena("FrameGraph Arena", 131072),
          mResourceSlots(mArena),
          mResources(mArena),
          mResourceNodes(mArena),
          mPassNodes(mArena)
{
    mResourceSlots.reserve(256);
    mResources.reserve(256);
    mResourceNodes.reserve(256);
    mPassNodes.reserve(64);
}

FrameGraph::~FrameGraph() = default;

FrameGraph& FrameGraph::compile() noexcept {
    DependencyGraph& dependencyGraph = mGraph;

    // first we cull unreachable nodes
    dependencyGraph.cull();

    /*
     * update the reference counter of the resource themselves and
     * compute first/last users for active passes
     */

    for (auto& pPassNode : mPassNodes) {
        if (pPassNode->isCulled()) {
            continue;
        }

        auto const& reads = dependencyGraph.getIncomingEdges(pPassNode.get());
        for (auto const& edge : reads) {
            // all incoming edges should be valid by construction
            assert(dependencyGraph.isEdgeValid(edge));
            auto pNode = static_cast<ResourceNode*>(dependencyGraph.getNode(edge->from));
            VirtualResource* pResource = getResource(pNode->resourceHandle);
            pResource->neededByPass(pPassNode.get());
        }

        auto const& writes = dependencyGraph.getOutgoingEdges(pPassNode.get());
        for (auto const& edge : writes) {
            // an outgoing edge might be invalid if the node it points to has been culled
            // but, because we are not culled and we're a pass, we add a reference to
            // the resource we are writing to.
            auto pNode = static_cast<ResourceNode*>(dependencyGraph.getNode(edge->to));
            VirtualResource* pResource = getResource(pNode->resourceHandle);
            pResource->neededByPass(pPassNode.get());
        }

        pPassNode->resolve();
    }

    /*
     * Resolve Usage bits
     */
    for (auto& pNode : mResourceNodes) {
        pNode->resolveResourceUsage(dependencyGraph);
    }

    dependencyGraph.export_graphviz(utils::slog.d);
    return *this;
}

void FrameGraph::execute(backend::DriverApi& driver) noexcept {
    auto const& passNodes = mPassNodes;
    auto const& resourcesList = mResources;
    auto& resourceAllocator = mResourceAllocator;

    driver.pushGroupMarker("FrameGraph");
    for (auto const& node : passNodes) {
        if (!node->isCulled()) {
            driver.pushGroupMarker(node->getName());

            // devirtualize resourcesList
            for (auto& pResource : resourcesList) {
                if (pResource->first == node.get()) {
                    pResource->devirtualize(resourceAllocator);
                }
            }

            // call execute
            FrameGraphResources resources(*this, *node);
            node->execute(resources, driver);

            // destroy declared render targets

            // destroy resourcesList
            for (auto& pResource : resourcesList) {
                if (pResource->last == node.get()) {
                    pResource->destroy(resourceAllocator);
                }
            }

            driver.popGroupMarker();
        }
    }
    // this is a good place to kick the GPU, since we've just done a bunch of work
    driver.flush();
    driver.popGroupMarker();
    reset();
}

void FrameGraph::addPresentPass(std::function<void(FrameGraph::Builder&)> setup) noexcept {
    PresentPassNode* node = mArena.make<PresentPassNode>(*this);
    mPassNodes.emplace_back(node, mArena);
    Builder builder(*this, *node);
    setup(builder);
    builder.sideEffect();
}

FrameGraph::Builder FrameGraph::addPassInternal(char const* name, PassExecutor* base) noexcept {
    // record in our pass list and create the builder
    PassNode* node = mArena.make<RenderPassNode>(*this, name, base);
    mPassNodes.emplace_back(node, mArena);
    return Builder(*this, *node);
}

FrameGraphHandle FrameGraph::addResourceInternal(UniquePtr<VirtualResource> resource) noexcept {
    FrameGraphHandle handle(mResourceSlots.size());
    ResourceSlot& slot = mResourceSlots.emplace_back();
    slot.rid = mResources.size();
    slot.nid = mResourceNodes.size();
    mResources.push_back(std::move(resource));
    ResourceNode* node = mArena.make<ResourceNode>(*this, handle);
    mResourceNodes.emplace_back(node, mArena);
    return handle;
}

FrameGraphHandle FrameGraph::readInternal(FrameGraphHandle handle,
        ResourceNode** pNode, VirtualResource** pResource) noexcept {
    *pNode = nullptr;
    *pResource = nullptr;
    if (!handle.isValid()) {
        return handle;
    }

    ResourceSlot const& slot = getResourceSlot(handle);
    assert((size_t)slot.rid < mResources.size());
    assert((size_t)slot.nid < mResourceNodes.size());
    *pResource = mResources[slot.rid].get();
    *pNode = mResourceNodes[slot.nid].get();
    return handle;
}

FrameGraphHandle FrameGraph::writeInternal(FrameGraphHandle handle,
        ResourceNode** pNode, VirtualResource** pResource) noexcept {
    *pNode = nullptr;
    *pResource = nullptr;
    if (!handle.isValid()) {
        return handle;
    }

    // update the slot with new ResourceNode index
    ResourceSlot& slot = getResourceSlot(handle);
    assert((size_t)slot.rid < mResources.size());
    assert((size_t)slot.nid < mResourceNodes.size());
    *pResource = mResources[slot.rid].get();
    *pNode = mResourceNodes[slot.nid].get();

    if (!(*pNode)->hasWriter()) {
        // this just means the resource was just created and was never written to.
        return handle;
    }

    // create a new handle with next version number
    handle.version++;

    // create new ResourceNodes
    slot.nid = mResourceNodes.size();
    *pNode = mArena.make<ResourceNode>(*this, handle);
    mResourceNodes.emplace_back(*pNode, mArena);

    // update version number in resource
    (*pResource)->version = handle.version;

    return handle;
}

void FrameGraph::reset() noexcept {
    // the order of destruction is important here
    mPassNodes.clear();
    mResourceNodes.clear();
    mResources.clear();
    mResourceSlots.clear();
}

} // namespace filament::fg2
