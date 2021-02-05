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

void ResourceNode::onCulled(DependencyGraph* graph) noexcept {
}

char const* ResourceNode::getName() const noexcept {
    return mFrameGraph.getResource(resourceHandle)->name;
}

void ResourceNode::addOutgoingEdge(DependencyGraph::Edge* edge) noexcept {
    mReaders.push_back(edge);
}

void ResourceNode::setIncomingEdge(DependencyGraph::Edge* edge) noexcept {
    assert(mWriter == nullptr);
    mWriter = edge;
}

bool ResourceNode::hasActiveReaders() const noexcept {
    DependencyGraph& dependencyGraph = mFrameGraph.getGraph();
    for (auto const& reader : mReaders) {
        if (!dependencyGraph.getNode(reader->to)->isCulled()) {
            return true;
        }
    }
    return false;
}

void ResourceNode::resolveResourceUsage(DependencyGraph& graph) noexcept {
    VirtualResource* pResource = mFrameGraph.getResource(resourceHandle);
    assert(pResource);
    if (pResource->refcount) {
        pResource->resolveUsage(graph, mReaders.data(), mReaders.size(), mWriter);
    }
}

utils::CString ResourceNode::graphvizify() const noexcept {
    std::string s;
    s.reserve(128);

    uint32_t id = getId();
    const char* const nodeName = getName();
    VirtualResource* const pResource = mFrameGraph.getResource(resourceHandle);

    s.append("[label=\"");
    s.append(nodeName);
    s.append("\\nrefs: ");
    s.append(std::to_string(pResource->refcount));
    s.append(", id: ");
    s.append(std::to_string(id));
    s.append("\\nversion: ");
    s.append(std::to_string(pResource->version));
    if (pResource->isImported()) {
        s.append(", imported");
    }
    s.append("\\nusage: ");
    s.append(pResource->usageString().c_str());
    s.append("\", ");

    s.append("style=filled, fillcolor=");
    s.append(pResource->refcount ? "skyblue" : "skyblue4");
    s.append("]");
    s.shrink_to_fit();

    return utils::CString{ s.c_str() };
}

utils::CString ResourceNode::graphvizifyEdgeColor() const noexcept {
    return utils::CString{"darkolivegreen"};
}

} // namespace filament::fg2
