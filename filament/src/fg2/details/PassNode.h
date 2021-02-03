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

#ifndef TNT_FILAMENT_FG2_PASSNODE_H
#define TNT_FILAMENT_FG2_PASSNODE_H

#include "fg2/details/DependencyGraph.h"
#include "fg2/details/Utilities.h"
#include "fg2/FrameGraph.h"
#include "fg2/RenderTarget.h"
#include "private/backend/DriverApiForward.h"

namespace utils {
class CString;
} // namespace utils

namespace filament::fg2 {

class FrameGraph;
class FrameGraphResources;
class PassExecutor;
class ResourceNode;

class PassNode : public DependencyGraph::Node {
public:
    using DependencyGraph::Node::Node;
    PassNode(PassNode&& rhs) noexcept;
    PassNode(PassNode const&) = delete;
    PassNode& operator=(PassNode const&) = delete;
    ~PassNode() noexcept override;
    using NodeID = DependencyGraph::NodeID;

    virtual void execute(FrameGraphResources const& resources, backend::DriverApi& driver) noexcept = 0;
    virtual void resolve() noexcept = 0;
};

class RenderPassNode : public PassNode {
public:
    struct RenderTargetData {
        RenderTarget::Descriptor descriptor;
        ResourceNode* incoming[6] = {};  // nodes of the incoming attachments
        ResourceNode* outgoing[6] = {};  // nodes of the outgoing attachments
        backend::Handle<backend::HwRenderTarget> target;
        backend::RenderPassParams params;
    };

    RenderPassNode(FrameGraph& fg, const char* name, PassExecutor* base) noexcept;
    RenderPassNode(RenderPassNode&& rhs) noexcept;
    ~RenderPassNode() noexcept override;

    RenderTarget declareRenderTarget(FrameGraph& fg, FrameGraph::Builder& builder,
            RenderTarget::Descriptor const& descriptor) noexcept;

    RenderTargetData const& getRenderTargetData(uint32_t id) const noexcept;

    // constants
    const char* const name = nullptr;                   // our name
    UniquePtr<PassExecutor, LinearAllocatorArena> base; // type eraser for calling execute()

private:
    // virtuals from DependencyGraph::Node
    char const* getName() const override { return name; }
    void onCulled(DependencyGraph* graph) override;
    utils::CString graphvizify() const override;
    void execute(FrameGraphResources const& resources, backend::DriverApi& driver) noexcept override;
    void resolve() noexcept override;

    std::vector<RenderTargetData> mRenderTargetData;
};

class PresentPassNode : public PassNode {
public:
    PresentPassNode(FrameGraph& fg) noexcept;
    PresentPassNode(PresentPassNode&& rhs) noexcept;
    ~PresentPassNode() noexcept override;
    PresentPassNode(PresentPassNode const&) = delete;
    PresentPassNode& operator=(PresentPassNode const&) = delete;
    void execute(FrameGraphResources const& resources, backend::DriverApi& driver) noexcept override;
    void resolve() noexcept override;
private:
    // virtuals from DependencyGraph::Node
    char const* getName() const override;
    void onCulled(DependencyGraph* graph) override;
    utils::CString graphvizify() const override;
};

} // namespace filament::fg2

#endif //TNT_FILAMENT_FG2_PASSNODE_H
