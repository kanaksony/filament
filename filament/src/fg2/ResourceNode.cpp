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
#include "fg2/details/ResourceNode.h"

namespace filament::fg2 {

ResourceNode::ResourceNode(FrameGraph& fg, FrameGraphHandle h) noexcept
        : DependencyGraph::Node(fg.getGraph()),
          resourceHandle(h), mFrameGraph(fg) {
}

ResourceNode::~ResourceNode() noexcept {
    VirtualResource* resource = mFrameGraph.getResource(resourceHandle);
    assert(resource);
    resource->destroyEdge(mWriter);
    for (auto* pEdge : mReaders) {
        resource->destroyEdge(pEdge);
    }
}

void ResourceNode::onCulled(DependencyGraph* graph) {
}

char const* ResourceNode::getName() const {
    return mFrameGraph.getResource(resourceHandle)->name;
}

void ResourceNode::addOutgoingEdge(DependencyGraph::Edge* edge) noexcept {
    mReaders.push_back(edge);
}

void ResourceNode::setIncomingEdge(DependencyGraph::Edge* edge) noexcept {
    assert(mWriter == nullptr);
    mWriter = edge;
}

utils::CString ResourceNode::graphvizify() const {
    std::string s;

    uint32_t id = getId();
    const char* const nodeName = getName();
    uint32_t refCount = getRefCount();
    VirtualResource* const pResource = mFrameGraph.getResource(resourceHandle);

    s.append("[label=\"");
    s.append(nodeName);
    s.append("\\nrefs: ");
    s.append(std::to_string(pResource->refcount));
    s.append(", id: ");
    s.append(std::to_string(id));
    s.append("\\nversion: ");
    s.append(std::to_string(pResource->version));
    if (pResource->imported) {
        s.append(", imported");
    }
    s.append("\", ");

    s.append("style=filled, fillcolor=");
    s.append(refCount ? "skyblue" : "skyblue4");
    s.append("]");

    return utils::CString{ s.c_str() };
}
} // namespace filament::fg2
