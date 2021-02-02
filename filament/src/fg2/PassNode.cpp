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

#include <string>

namespace filament::fg2 {

PassNode::PassNode(PassNode&& rhs) noexcept = default;
PassNode::~PassNode() noexcept = default;

// ------------------------------------------------------------------------------------------------

RenderPassNode::RenderPassNode(FrameGraph& fg, const char* name, PassExecutor* base) noexcept
        : PassNode(fg.getGraph()), name(name), base(base, fg.getArena()) {
}
RenderPassNode::RenderPassNode(RenderPassNode&& rhs) noexcept = default;
RenderPassNode::~RenderPassNode() noexcept = default;

void RenderPassNode::onCulled(DependencyGraph* graph) {
}

utils::CString RenderPassNode::graphvizify() const {
    std::string s;

    uint32_t id = getId();
    const char* const nodeName = getName();
    uint32_t refCount = getRefCount();

    s.append("[label=\"");
    s.append(nodeName);
    s.append("\\nrefs: ");
    s.append(std::to_string(refCount));
    s.append(", id: ");
    s.append(std::to_string(id));
    s.append("\", ");

    s.append("style=filled, fillcolor=");
    s.append(refCount ? "darkorange" : "darkorange4");
    s.append("]");

    return utils::CString{ s.c_str() };
}

void RenderPassNode::execute(
        FrameGraphResources const& resources, backend::DriverApi& driver) noexcept {
    base->execute(resources, driver);
}

RenderTarget RenderPassNode::declareRenderTarget(FrameGraph& fg, FrameGraph::Builder& builder,
        RenderTarget::Descriptor const& descriptor) noexcept {

    RenderTargetData data;
    data.descriptor = descriptor;
    RenderTarget::Attachments& attachments = data.descriptor.attachments;

    // retrieve the ResourceNode of the attachments coming to us -- this will be used later
    // to compute the discard flags.
    for (size_t i = 0; i < 4; i++) {
        if (descriptor.attachments.color[i].isValid()) {
            data.incoming[i] = fg.getResourceNode(attachments.color[i]);
            attachments.color[i] = builder.write(attachments.color[i],
                    Texture::Usage::COLOR_ATTACHMENT);
            data.outgoing[i] = fg.getResourceNode(attachments.color[i]);
        }
    }
    if (descriptor.attachments.depth.isValid()) {
        data.incoming[4] = fg.getResourceNode(attachments.depth);
        attachments.depth = builder.write(attachments.depth,
                Texture::Usage::DEPTH_ATTACHMENT);
        data.outgoing[4] = fg.getResourceNode(attachments.depth);
    }
    if (descriptor.attachments.stencil.isValid()) {
        data.incoming[5] = fg.getResourceNode(attachments.stencil);
        attachments.stencil = builder.write(attachments.stencil,
                Texture::Usage::STENCIL_ATTACHMENT);
        data.outgoing[5] = fg.getResourceNode(attachments.stencil);
    }

    uint32_t id = mRenderTargetData.size();
    mRenderTargetData.push_back(data);
    return { data.descriptor.attachments, id };
}

// ------------------------------------------------------------------------------------------------

PresentPassNode::PresentPassNode(FrameGraph& fg) noexcept
        : PassNode(fg.getGraph()) {
}
PresentPassNode::PresentPassNode(PresentPassNode&& rhs) noexcept = default;
PresentPassNode::~PresentPassNode() noexcept = default;

char const* PresentPassNode::getName() const {
    return "Present";
}

void PresentPassNode::onCulled(DependencyGraph* graph) {
}

utils::CString PresentPassNode::graphvizify() const {
    std::string s;
    s.reserve(128);
    uint32_t id = getId();
    s.append("[label=\"Present , id: ");
    s.append(std::to_string(id));
    s.append("\", style=filled, fillcolor=red3]");
    s.shrink_to_fit();
    return utils::CString{ s.c_str() };
}

void PresentPassNode::execute(FrameGraphResources const&, backend::DriverApi&) noexcept {
}

} // namespace filament::fg2
