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

#ifndef TNT_FILAMENT_FG2_RESOURCE_H
#define TNT_FILAMENT_FG2_RESOURCE_H

#include "fg2/FrameGraphId.h"
#include "fg2/Texture.h"
#include "fg2/RenderTarget.h"
#include "fg2/details/DependencyGraph.h"

namespace filament {
class ResourceAllocatorInterface;
} // namespace::filament

namespace filament::fg2 {

class PassNode;
class ResourceNode;
class ImportedRenderTarget;

/*
 * ResourceEdgeBase only exists to enforce type safety
 */
class ResourceEdgeBase : public DependencyGraph::Edge {
public:
    using DependencyGraph::Edge::Edge;
};

/*
 * The generic parts of virtual resources.
 */
class VirtualResource {
public:
    // constants
    const char* const name;

    // updated by builder
    FrameGraphHandle::Version version = 0;

    // computed during compile()
    uint32_t refcount = 0;
    PassNode* first = nullptr;  // pass that needs to instantiate the resource
    PassNode* last = nullptr;   // pass that can destroy the resource

    VirtualResource(const char* name) noexcept : name(name) { }
    VirtualResource(VirtualResource const&) = delete;
    VirtualResource& operator=(VirtualResource const&) = delete;
    virtual ~VirtualResource() noexcept;

    // updates first/last/refcount
    void neededByPass(PassNode* pNode) noexcept;

    /*
     * Called during FrameGraph::compile(), this gives an opportunity for this resource to
     * calculate its effective usage flags.
     */
    virtual void resolveUsage(DependencyGraph& graph,
            ResourceEdgeBase const* const* edges, size_t count,
            ResourceEdgeBase const* writer) noexcept = 0;

    /* Instantiate the concrete resource */
    virtual void devirtualize(ResourceAllocatorInterface& resourceAllocator) noexcept = 0;

    /* Destroy the concrete resource */
    virtual void destroy(ResourceAllocatorInterface& resourceAllocator) noexcept = 0;

    /* Destroy an Edge instantiated by this resource */
    virtual void destroyEdge(DependencyGraph::Edge* edge) noexcept = 0;

    virtual utils::CString usageString() const noexcept = 0;

    virtual bool isImported() const noexcept { return false; }

    // this is to workaround our lack of RTTI -- otherwise we could use dynamic_cast
    virtual ImportedRenderTarget* asImportedRenderTarget() noexcept { return nullptr; }

protected:
    void addOutgoingEdge(ResourceNode* node, ResourceEdgeBase* edge) noexcept;
    void setIncomingEdge(ResourceNode* node, ResourceEdgeBase* edge) noexcept;
    // these exist only so we don't have to include PassNode.h or ResourceNode.h
    static DependencyGraph::Node* toDependencyGraphNode(ResourceNode* node) noexcept;
    static DependencyGraph::Node* toDependencyGraphNode(PassNode* node) noexcept;
};

// ------------------------------------------------------------------------------------------------

/*
 * Resource specific parts of a VirtualResource
 */
template<typename RESOURCE>
class Resource : public VirtualResource {
    using Usage = typename RESOURCE::Usage;

public:
    // valid only after devirtualize() has been called
    RESOURCE resource{};

    // valid only after resolveUsage() has been called
    Usage usage{};

    using Descriptor = typename RESOURCE::Descriptor;
    using SubResourceDescriptor = typename RESOURCE::SubResourceDescriptor;

    // our concrete (sub)resource descriptors -- used to create it.
    Descriptor descriptor;
    SubResourceDescriptor subResourceDescriptor;

    // An Edge with added data from this resource
    class ResourceEdge : public ResourceEdgeBase {
    public:
        Usage usage;
        ResourceEdge(DependencyGraph& graph,
                DependencyGraph::Node* from, DependencyGraph::Node* to, Usage usage) noexcept
                : ResourceEdgeBase(graph, from, to), usage(usage) {
        }
    };

    Resource(const char* name, Descriptor const& desc) noexcept
        : VirtualResource(name), descriptor(desc) {
    }

    ~Resource() noexcept = default;

    // pass Node to resource Node edge (a write to)
    virtual bool connect(DependencyGraph& graph,
            PassNode* passNode, ResourceNode* resourceNode, Usage u) noexcept {
        auto* edge = new ResourceEdge(graph,
                toDependencyGraphNode(passNode), toDependencyGraphNode(resourceNode), u);
        setIncomingEdge(resourceNode, edge);
        // TODO: we should check that usage flags are correct (e.g. a write flag is not used for reading)
        return true;
    }

    // resource Node to pass Node edge (a read from)
    virtual bool connect(DependencyGraph& graph,
            ResourceNode* resourceNode, PassNode* passNode, Usage u) noexcept {
        auto* edge = new ResourceEdge(graph,
                toDependencyGraphNode(resourceNode), toDependencyGraphNode(passNode), u);
        addOutgoingEdge(resourceNode, edge);
        // TODO: we should check that usage flags are correct (e.g. a write flag is not used for reading)
        return true;
    }

protected:
    /*
     * The virtual below must be in a header file as RESOURCE is only known at compile time
     */

    void resolveUsage(DependencyGraph& graph,
            ResourceEdgeBase const* const* edges, size_t count,
            ResourceEdgeBase const* writer) noexcept override {
        for (size_t i = 0; i < count; i++) {
            if (graph.isEdgeValid(edges[i])) {
                // this Edge is guaranteed to be a ResourceEdge<RESOURCE> by construction
                ResourceEdge const* const edge = static_cast<ResourceEdge const*>(edges[i]);
                usage |= edge->usage;
            }
            if (writer) {
                ResourceEdge const* const edge = static_cast<ResourceEdge const*>(writer);
                usage |= edge->usage;
            }
        }
    }

    void destroyEdge(DependencyGraph::Edge* edge) noexcept override {
        // this Edge is guaranteed to be a ResourceEdge<RESOURCE> by construction
        delete static_cast<ResourceEdge *>(edge);
    }

    void devirtualize(ResourceAllocatorInterface& resourceAllocator) noexcept override {
        resource.create(resourceAllocator, name, descriptor, usage);
    }

    void destroy(ResourceAllocatorInterface& resourceAllocator) noexcept override {
        resource.destroy(resourceAllocator);
    }

    utils::CString usageString() const noexcept override {
        return utils::to_string(usage);
    }
};

/*
 * An imported resource is just like a regular one, except that it's constructed directly from
 * the concrete resource and it, evidently, doesn't create/destroy the concrete resource.
 */
template<typename RESOURCE>
class ImportedResource : public Resource<RESOURCE> {
public:
    using Descriptor = typename RESOURCE::Descriptor;
    ImportedResource(const char* name, Descriptor const& desc, RESOURCE const& rsrc) noexcept
            : Resource<RESOURCE>(name, desc) {
        this->resource = rsrc;
    }

private:
    void devirtualize(ResourceAllocatorInterface&) noexcept override {
        // imported resources don't need to devirtualize
    }
    void destroy(ResourceAllocatorInterface&) noexcept override {
        // imported resources never destroy the concrete resource
    }

    bool isImported() const noexcept override { return true; }
};


class ImportedRenderTarget : public ImportedResource<Texture> {
public:
    backend::Handle<backend::HwRenderTarget> target;
    RenderTarget::Descriptor rtdesc;

    using Descriptor = Texture::Descriptor;
    ImportedRenderTarget(const char* name, Descriptor const& tdesc,
            RenderTarget::Descriptor const& desc,
            backend::Handle<backend::HwRenderTarget> target);

    ~ImportedRenderTarget() noexcept override;

    bool connect(DependencyGraph& graph, PassNode* passNode, ResourceNode* resourceNode,
            Texture::Usage u) noexcept override;

    bool connect(DependencyGraph& graph, ResourceNode* resourceNode, PassNode* passNode,
            Texture::Usage u) noexcept override;

    ImportedRenderTarget* asImportedRenderTarget() noexcept override { return this; }
};


} // namespace filament::fg2

#endif //TNT_FILAMENT_FG2_RESOURCE_H
