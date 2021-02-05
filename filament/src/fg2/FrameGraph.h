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

#ifndef TNT_FILAMENT_FG2_FRAMEGRAPH_H
#define TNT_FILAMENT_FG2_FRAMEGRAPH_H

#include "fg2/FrameGraphId.h"
#include "fg2/Pass.h"
#include "fg2/RenderTarget.h"
#include "fg2/Texture.h"

#include "fg2/details/DependencyGraph.h"
#include "fg2/details/Resource.h"
#include "fg2/details/Utilities.h"

#include "details/Allocators.h"

#include "private/backend/DriverApiForward.h"

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <functional>

namespace filament {

class ResourceAllocatorInterface;

namespace fg2 {

class PassExecutor;
class PassNode;
class ResourceNode;
class VirtualResource;

class FrameGraph {
public:

    class Builder {
    public:
        Builder(Builder const&) = delete;
        Builder& operator=(Builder const&) = delete;

        /**
         * Declare a RenderTarget for this pass. All subresource handles get new versions after
         * this call. The new values are available in the returned RenderTarget.
         * useAsRenderTarget() implies a write(). If a read() is also needed, it must be
         * issued separately before calling useAsRenderTarget().
         *
         * @param name  A pointer to a null terminated string.
         *              The pointer lifetime must extend beyond execute()
         * @param desc  Descriptor for the render target.
         * @return      A RenderTarget structure containing the new subresource handles as well as
         *              an id to retrieve the concrete render target in the execute phase.
         */
        RenderTarget useAsRenderTarget(const char* name,
                RenderTarget::Descriptor const& desc) noexcept;

        /**
         * Helper to easily declare a render target with a single color attachment.
         * This is equivalent to:
         * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
         *      auto [attachments, id] = useAsRenderTarget(getName(*color),
         *              {.attachments = {.color = {*color}}});
         *      *color = attachments.color[0];
         *      return id;
         * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
         *
         * @param color color attachment subresource
         * @return the id of this Render Target
         */
        uint32_t useAsRenderTarget(FrameGraphId<Texture>* color) noexcept;

        /**
         * Helper to easily declare a render target with a color and depth attachment.
         * This is equivalent to:
         * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
         *      auto [attachments, id] = useAsRenderTarget(getName(color ? *color : *depth),
         *              {.attachments = {.color = {*color}, .depth = *depth}});
         *      *color = attachments.color[0];
         *      *depth = attachments.depth;
         *      return id;
         * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
         * Either color or depth must be non-null.
         *
         * @param color color attachment subresource, can be null
         * @param depth depth attachment subresource, can be null
         * @return the id of this Render Target
         */
        uint32_t useAsRenderTarget(FrameGraphId<Texture>* color,
                FrameGraphId<Texture>* depth) noexcept;


        /**
         * Creates a virtual resource of type RESOURCE
         * @tparam RESOURCE Type of the resource to create
         * @param name      A pointer to a null terminated string.
         *                  The pointer lifetime must extend beyond execute().
         * @param desc      Descriptor for this resources
         * @return          A typed resource handle
         */
        template<typename RESOURCE>
        FrameGraphId<RESOURCE> create(const char* name,
                typename RESOURCE::Descriptor const& desc = {}) noexcept {
            return mFrameGraph.create<RESOURCE>(name, desc);
        }


        /**
         * Creates a subresource of the virtual resource of type RESOURCE. This adds a reference
         * from the subresource to the resource.
         *
         * @tparam RESOURCE     Type of the virtual resource
         * @param parent        Pointer to the handle of parent resource. This will be updated.
         * @param name          A name for the subresource
         * @param desc          Descriptor of the subresource
         * @return              A handle to the subresource
         */
        template<typename RESOURCE>
        inline FrameGraphId<RESOURCE> createSubresource(FrameGraphId<RESOURCE>* parent,
                const char* name,
                typename RESOURCE::SubResourceDescriptor const& desc = {}) noexcept;


        /**
         * Declares a read access by this pass to a virtual resource. This adds a reference from
         * the pass to the resource.
         * @tparam RESOURCE Type of the resource
         * @param input     Handle to the resource
         * @param usage     How is this resource used. e.g.: sample vs. upload for textures. This is resource dependant.
         * @return          A new handle to the resource. The input handle is no-longer valid.
         */
        template<typename RESOURCE>
        inline FrameGraphId<RESOURCE> read(FrameGraphId<RESOURCE> input,
                typename RESOURCE::Usage usage = {}) {
            return mFrameGraph.read<RESOURCE>(mPass, input, usage);
        }

        /**
         * Declares a write access by this pass to a virtual resource. This adds a reference from
         * the resource to the pass.
         * @tparam RESOURCE Type of the resource
         * @param input     Handle to the resource
         * @param usage     How is this resource used. This is resource dependant.
         * @return          A new handle to the resource. The input handle is no-longer valid.
         */
        template<typename RESOURCE>
        FrameGraphId<RESOURCE> write(FrameGraphId<RESOURCE> input,
                typename RESOURCE::Usage usage = {}) {
            return mFrameGraph.write<RESOURCE>(mPass, input, usage);
        }

        /**
         * Marks the current pass as a leaf. Adds a reference to it, so it's not culled.
         */
        void sideEffect() noexcept;

        /**
         * Retrieves the descriptor associated to a resource
         * @tparam RESOURCE Type of the resource
         * @param handle    Handle to a virtual resource
         * @return          Reference to the descriptor
         */
        template<typename RESOURCE>
        typename RESOURCE::Descriptor const& getDescriptor(FrameGraphId<RESOURCE> handle) const {
            return static_cast<Resource<RESOURCE> const*>(
                    mFrameGraph.getResource(handle))->descriptor;
        }

        /**
         * Retrieves the name of a resource
         * @param handle    Handle to a virtual resource
         * @return          C string to the name of the resource
         */
        const char* getName(FrameGraphHandle handle) const noexcept;
    private:
        friend class FrameGraph;
        Builder(FrameGraph& fg, PassNode& pass) noexcept;
        ~Builder() noexcept;
        FrameGraph& mFrameGraph;
        PassNode& mPass;
    };

    // --------------------------------------------------------------------------------------------

    explicit FrameGraph(ResourceAllocatorInterface& resourceAllocator);
    FrameGraph(FrameGraph const&) = delete;
    FrameGraph& operator=(FrameGraph const&) = delete;
    ~FrameGraph();

    /**
     * Add a pass to the frame graph. Typically:
     *
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     * struct PassData {
     * };
     * auto& pass = addPass<PassData>("Pass Name",
     *      [&](Builder& builder, auto& data) {
     *          // synchronously declare resources here
     *      },
     *      [=](Resources const& resources, auto const&, DriverApi& driver) {
     *          // issue backend drawing commands here
     *      }
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     *
     * @tparam Data     A user-defined structure containing this pass data
     * @tparam Setup    A lambda of type [](Builder&, Data&).
     * @tparam Execute  A lambda of type [](Resources const&, Data const&, DriverApi&)
     *
     * @param name      A name for this pass. Used for debugging only.
     * @param setup     lambda called synchronously, used to declare which and how resources are
     *                  used by this pass. Captures should be done by reference.
     * @param execute   lambda called asynchronously from FrameGraph::execute(),
     *                  where immediate drawing commands can be issued.
     *                  Captures must be done by copy.
     *
     * @return          A reference to a Pass object
     */
    template<typename Data, typename Setup, typename Execute>
    Pass<Data, Execute>& addPass(const char* name, Setup setup, Execute&& execute);

    /**
     * Allocates concrete resources and culls unreferenced passes.
     * @return a reference to the FrameGraph, for chaining calls.
     */
    FrameGraph& compile() noexcept;

    /**
     * Execute all referenced passes
     *
     * @param driver a reference to the backend to execute the commands
     */
    void execute(backend::DriverApi& driver) noexcept;


    /**
     * Moves the resource associated to the handle 'from' to the handle 'to'. After this call,
     * all handles referring to the resource 'to' are redirected to the resource 'from'
     * (including handles used in the past).
     * All writes to 'from' are disconnected (i.e. these passes loose a reference).
     *
     * @tparam RESOURCE     Type of the resources
     * @param from          Resource to be moved
     * @param to            Resource to be replaced
     */

    /**
     * Forwards a subresource to another one which gets replaced. A new version of the forwarded
     * resource is created (similar to a write), and the replaced resource's handle becomes forever
     * invalid.
     *
     * @tparam RESOURCE             Type of the resources
     * @param subresource           Handle to the subresource being forwarded
     * @param replacedSubresource   Pointer to the handle of the subresource being replaced
     *                              This handle becomes invalid after this call
     * @return                      Handle to a new version of the forwarded subresource
     */
    template<typename RESOURCE>
    FrameGraphId<RESOURCE> forwardSubResource(FrameGraphId<RESOURCE> subresource,
            FrameGraphId<RESOURCE>* replacedSubresource);

    /**
     * Adds a reference to 'input', preventing it from being culled
     *
     * @param input a resource handle
     */
    template<typename RESOURCE>
    void present(FrameGraphId<RESOURCE> input);

    /**
     * Imports a concrete resource to the frame graph. The lifetime management is not transferred
     * to the frame graph.
     *
     * @tparam RESOURCE     Type of the resource to import
     * @param name          A name for this resource
     * @param desc          The descriptor for this resource
     * @param resource      A reference to the resource itself
     * @return              A handle that can be used normally in the frame graph
     */
    template<typename RESOURCE>
    FrameGraphId<RESOURCE> import(const char* name,
            typename RESOURCE::Descriptor const& desc, const RESOURCE& resource) noexcept;

    /**
     * Imports a RenderTarget as a Texture into the frame graph. Later, this
     * Texture can be used with useAsRenderTarget(), the resulting concrete RenderTarget
     * will be the one passed as argument here, instead of being dynamically created.
     *
     * @param name      A name for the rendter target
     * @param desc      Descriptor for the imported Texture
     * @param target    handle to the concrete render target to import
     * @return          A handle to a Texture
     */
    FrameGraphId<Texture> import(const char* name, const Texture::Descriptor& desc,
            backend::Handle<backend::HwRenderTarget> target);

private:
    friend class FrameGraphResources;
    friend class PassNode;
    friend class ResourceNode;
    friend class RenderPassNode;

    template<typename T> using UniquePtr = std::unique_ptr<T, Deleter<T, LinearAllocatorArena>>;
    template<typename T> using Allocator = utils::STLAllocator<T, LinearAllocatorArena>;
    template<typename T> using Vector = std::vector<T, Allocator<T>>; // 32 bytes

    LinearAllocatorArena& getArena() noexcept { return mArena; }
    DependencyGraph& getGraph() noexcept { return mGraph; }
    ResourceAllocatorInterface& getResourceAllocator() noexcept { return mResourceAllocator; }

    struct ResourceSlot {
        int16_t rid;    // VirtualResource* index
        int16_t nid;    // ResourceNode* index
    };
    void reset() noexcept;
    void addPresentPass(std::function<void(Builder&)> setup) noexcept;
    Builder addPassInternal(const char* name, PassExecutor* base) noexcept;
    FrameGraphHandle addResourceInternal(UniquePtr<VirtualResource> resource) noexcept;
    FrameGraphHandle readInternal(FrameGraphHandle handle, ResourceNode** pNode, VirtualResource** pResource) noexcept;
    FrameGraphHandle writeInternal(FrameGraphHandle handle, ResourceNode** pNode, VirtualResource** pResource) noexcept;

    template<typename RESOURCE>
    FrameGraphId<RESOURCE> create(char const* name,
            typename RESOURCE::Descriptor const& desc) noexcept;

    template<typename RESOURCE>
    FrameGraphId<RESOURCE> read(PassNode& passNode,
            FrameGraphId<RESOURCE> input, typename RESOURCE::Usage usage);

    template<typename RESOURCE>
    FrameGraphId<RESOURCE> write(PassNode& passNode,
            FrameGraphId<RESOURCE> input, typename RESOURCE::Usage usage);

    ResourceSlot& getResourceSlot(FrameGraphHandle handle) noexcept {
        assert((size_t)handle.index < mResourceSlots.size());
        return mResourceSlots[handle.index];
    }

    VirtualResource* getResource(FrameGraphHandle handle) noexcept {
        ResourceSlot const& slot = getResourceSlot(handle);
        assert((size_t)slot.rid < mResources.size());
        return mResources[slot.rid].get();
    }

    ResourceNode* getResourceNode(FrameGraphHandle handle) noexcept {
        ResourceSlot const& slot = getResourceSlot(handle);
        assert((size_t)slot.nid < mResourceNodes.size());
        return mResourceNodes[slot.nid].get();
    }

    VirtualResource const* getResource(FrameGraphHandle handle) const noexcept {
        return const_cast<FrameGraph*>(this)->getResource(handle);
    }

    ResourceNode const* getResourceNode(FrameGraphHandle handle) const noexcept {
        return const_cast<FrameGraph*>(this)->getResourceNode(handle);
    }

    bool isValid(FrameGraphHandle handle) const noexcept {
        return handle.version == getResource(handle)->version;
    }

    ResourceAllocatorInterface& mResourceAllocator;
    LinearAllocatorArena mArena;
    DependencyGraph mGraph;

    // TODO: not sure that we need unique_ptr<>, it might be enough to just free the
    //       objects in the dtor and in reset(). We can fix that later.
    Vector<ResourceSlot> mResourceSlots;
    Vector<UniquePtr<VirtualResource>> mResources;
    Vector<UniquePtr<ResourceNode>> mResourceNodes;
    Vector<UniquePtr<PassNode>> mPassNodes;
};

template<typename Data, typename Setup, typename Execute>
Pass<Data, Execute>& FrameGraph::addPass(char const* name, Setup setup, Execute&& execute) {
    static_assert(sizeof(Execute) < 1024, "Execute() lambda is capturing too much data.");

    // create the FrameGraph pass
    auto* const pass = mArena.make<Pass<Data, Execute>>(std::forward<Execute>(execute));

    Builder builder(addPassInternal(name, pass));
    setup(builder, pass->getData());

    // return a reference to the pass to the user
    return *pass;
}

template<typename RESOURCE>
void FrameGraph::present(FrameGraphId<RESOURCE> input) {
    addPresentPass([&](Builder& builder) { builder.read(input); });
}

template<typename RESOURCE>
FrameGraphId<RESOURCE> FrameGraph::create(char const* name,
        typename RESOURCE::Descriptor const& desc) noexcept {
    UniquePtr<VirtualResource> vresource(mArena.make<Resource<RESOURCE>>(name, desc), mArena);
    return FrameGraphId<RESOURCE>(addResourceInternal(std::move(vresource)));
}

template<typename RESOURCE>
FrameGraphId<RESOURCE> FrameGraph::import(char const* name, typename RESOURCE::Descriptor const& desc,
        RESOURCE const& resource) noexcept {
    UniquePtr<VirtualResource> vresource(mArena.make<ImportedResource<RESOURCE>>(name, desc, resource), mArena);
    return FrameGraphId<RESOURCE>(addResourceInternal(std::move(vresource)));
}

template<typename RESOURCE>
FrameGraphId<RESOURCE> FrameGraph::read(PassNode& passNode, FrameGraphId<RESOURCE> input,
        typename RESOURCE::Usage usage) {
    ResourceNode* node;
    VirtualResource* vrsrc;
    FrameGraphId<RESOURCE> result(readInternal(input, &node, &vrsrc));
    if (result.isValid()) {
        Resource<RESOURCE>* resource = static_cast<Resource<RESOURCE> *>(vrsrc);
        resource->connect(mGraph, node, &passNode, usage);
    }
    return result;
}

template<typename RESOURCE>
FrameGraphId<RESOURCE> FrameGraph::write(PassNode& passNode, FrameGraphId<RESOURCE> input,
        typename RESOURCE::Usage usage) {
    ResourceNode* node;
    VirtualResource* vrsrc;
    FrameGraphId<RESOURCE> result(writeInternal(input, &node, &vrsrc));
    if (result.isValid()) {
        Resource<RESOURCE>* resource = static_cast<Resource<RESOURCE>*>(vrsrc);
        resource->connect(mGraph, &passNode, node, usage);
    }
    return result;
}

} // namespace fg2
} // namespace filament

#endif //TNT_FILAMENT_FG2_FRAMEGRAPH_H
