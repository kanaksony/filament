/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <gtest/gtest.h>

#include "ResourceAllocator.h"

#include <backend/Platform.h>

#include "private/backend/CommandStream.h"

#include "fg2/FrameGraph.h"
#include "fg2/FrameGraphResources.h"
#include "fg2/details/DependencyGraph.h"

using namespace filament;
using namespace backend;
using namespace fg2;

class MockResourceAllocator : public ResourceAllocatorInterface {
    uint32_t handle = 0;
public:
    backend::RenderTargetHandle createRenderTarget(const char* name,
            backend::TargetBufferFlags targetBufferFlags,
            uint32_t width,
            uint32_t height,
            uint8_t samples,
            backend::MRT color,
            backend::TargetBufferInfo depth,
            backend::TargetBufferInfo stencil) noexcept override {
        return backend::RenderTargetHandle(++handle);
    }

    void destroyRenderTarget(backend::RenderTargetHandle h) noexcept override {
    }

    backend::TextureHandle createTexture(const char* name, backend::SamplerType target,
            uint8_t levels,
            backend::TextureFormat format, uint8_t samples, uint32_t width, uint32_t height,
            uint32_t depth, backend::TextureUsage usage) noexcept override {
        return backend::TextureHandle(++handle);
    }

    void destroyTexture(backend::TextureHandle h) noexcept override {
    }
};

class FrameGraphTest : public testing::Test {
protected:
    Backend backend = Backend::NOOP;
    CircularBuffer buffer = CircularBuffer{ 8192 };
    DefaultPlatform* platform = DefaultPlatform::create(&backend);
    CommandStream driverApi = CommandStream{ *platform->createDriver(nullptr), buffer };
    MockResourceAllocator resourceAllocator;
    FrameGraph fg{resourceAllocator};
};

class Node : public DependencyGraph::Node {
    const char *mName;
    bool mCulledCalled = false;
    char const* getName() const noexcept override { return mName; }
    void onCulled(DependencyGraph* graph) noexcept override { mCulledCalled = true; }
public:
    Node(DependencyGraph& graph, const char* name) noexcept : DependencyGraph::Node(graph), mName(name) { }
    bool isCulledCalled() const noexcept { return mCulledCalled; }
};

TEST(DependencyGraphTest, Simple) {
    DependencyGraph graph;
    Node* n0 = new Node(graph, "node 0");
    Node* n1 = new Node(graph, "node 1");
    Node* n2 = new Node(graph, "node 2");

    new DependencyGraph::Edge(graph, n0, n1);
    new DependencyGraph::Edge(graph, n1, n2);
    n2->makeTarget();

    graph.cull();

    //graph.export_graphviz(utils::slog.d);

    EXPECT_FALSE(n2->isCulled());
    EXPECT_FALSE(n1->isCulled());
    EXPECT_FALSE(n0->isCulled());
    EXPECT_FALSE(n2->isCulledCalled());
    EXPECT_FALSE(n1->isCulledCalled());
    EXPECT_FALSE(n0->isCulledCalled());

    EXPECT_EQ(n0->getRefCount(), 1);
    EXPECT_EQ(n1->getRefCount(), 1);
    EXPECT_EQ(n2->getRefCount(), 1);

    auto edges = graph.getEdges();
    auto nodes = graph.getNodes();
    graph.clear();
    for (auto e : edges) { delete e; }
    for (auto n : nodes) { delete n; }
}

TEST(DependencyGraphTest, Culling1) {
    DependencyGraph graph;
    Node* n0 = new Node(graph, "node 0");
    Node* n1 = new Node(graph, "node 1");
    Node* n2 = new Node(graph, "node 2");
    Node* n1_0 = new Node(graph, "node 1.0");

    new DependencyGraph::Edge(graph, n0, n1);
    new DependencyGraph::Edge(graph, n1, n2);
    new DependencyGraph::Edge(graph, n1, n1_0);
    n2->makeTarget();

    graph.cull();

    //graph.export_graphviz(utils::slog.d);

    EXPECT_TRUE(n1_0->isCulled());
    EXPECT_TRUE(n1_0->isCulledCalled());

    EXPECT_FALSE(n2->isCulled());
    EXPECT_FALSE(n1->isCulled());
    EXPECT_FALSE(n0->isCulled());
    EXPECT_FALSE(n2->isCulledCalled());
    EXPECT_FALSE(n1->isCulledCalled());
    EXPECT_FALSE(n0->isCulledCalled());

    EXPECT_EQ(n0->getRefCount(), 1);
    EXPECT_EQ(n1->getRefCount(), 1);
    EXPECT_EQ(n2->getRefCount(), 1);

    auto edges = graph.getEdges();
    auto nodes = graph.getNodes();
    graph.clear();
    for (auto e : edges) { delete e; }
    for (auto n : nodes) { delete n; }
}

TEST(DependencyGraphTest, Culling2) {
    DependencyGraph graph;
    Node* n0 = new Node(graph, "node 0");
    Node* n1 = new Node(graph, "node 1");
    Node* n2 = new Node(graph, "node 2");
    Node* n1_0 = new Node(graph, "node 1.0");
    Node* n1_0_0 = new Node(graph, "node 1.0.0");
    Node* n1_0_1 = new Node(graph, "node 1.0.0");

    new DependencyGraph::Edge(graph, n0, n1);
    new DependencyGraph::Edge(graph, n1, n2);
    new DependencyGraph::Edge(graph, n1, n1_0);
    new DependencyGraph::Edge(graph, n1_0, n1_0_0);
    new DependencyGraph::Edge(graph, n1_0, n1_0_1);
    n2->makeTarget();

    graph.cull();

    //graph.export_graphviz(utils::slog.d);

    EXPECT_TRUE(n1_0->isCulled());
    EXPECT_TRUE(n1_0_0->isCulled());
    EXPECT_TRUE(n1_0_1->isCulled());
    EXPECT_TRUE(n1_0->isCulledCalled());
    EXPECT_TRUE(n1_0_0->isCulledCalled());
    EXPECT_TRUE(n1_0_1->isCulledCalled());

    EXPECT_FALSE(n2->isCulled());
    EXPECT_FALSE(n1->isCulled());
    EXPECT_FALSE(n0->isCulled());
    EXPECT_FALSE(n2->isCulledCalled());
    EXPECT_FALSE(n1->isCulledCalled());
    EXPECT_FALSE(n0->isCulledCalled());

    EXPECT_EQ(n0->getRefCount(), 1);
    EXPECT_EQ(n1->getRefCount(), 1);
    EXPECT_EQ(n2->getRefCount(), 1);

    auto edges = graph.getEdges();
    auto nodes = graph.getNodes();
    graph.clear();
    for (auto e : edges) { delete e; }
    for (auto n : nodes) { delete n; }
}

TEST_F(FrameGraphTest, Basic) {
    struct DepthPassData {
        FrameGraphId<Texture> depth;
    };
    auto& depthPass = fg.addPass<DepthPassData>("Depth pass",
            [&](FrameGraph::Builder& builder, auto& data) {
                data.depth = builder.create<Texture>("Depth Buffer", {.width=16, .height=32});
                builder.useAsRenderTarget(nullptr, &data.depth);
                EXPECT_TRUE(fg.isValid(data.depth));
            },
            [=](FrameGraphResources const& resources, auto const& data, backend::DriverApi& driver) {
                Texture const& depth = resources.get(data.depth);
                EXPECT_TRUE((bool)depth.texture);
                auto rp = resources.getRenderPassInfo();
                EXPECT_EQ(rp.params.flags.discardStart, TargetBufferFlags::DEPTH);
                EXPECT_EQ(rp.params.flags.discardEnd, TargetBufferFlags::NONE);
                EXPECT_EQ(rp.params.viewport.width, 16);
                EXPECT_EQ(rp.params.viewport.height, 32);
                EXPECT_TRUE((bool)rp.target);
            });

    struct GBufferPassData {
        FrameGraphId<Texture> depth;
        FrameGraphId<Texture> gbuf1;
        FrameGraphId<Texture> gbuf2;
        FrameGraphId<Texture> gbuf3;
    };
    auto& gBufferPass = fg.addPass<GBufferPassData>("Gbuffer pass",
            [&](FrameGraph::Builder& builder, auto& data) {
                data.depth = builder.read(depthPass->depth, Texture::Usage::DEPTH_ATTACHMENT);
                Texture::Descriptor desc = builder.getDescriptor(data.depth);
                data.gbuf1 = builder.create<Texture>("Gbuffer 1", desc);
                data.gbuf2 = builder.create<Texture>("Gbuffer 2", desc);
                data.gbuf3 = builder.create<Texture>("Gbuffer 3", desc);
                auto rt = builder.useAsRenderTarget("Gbuffer target", { .attachments = {
                                .color = { data.gbuf1, data.gbuf2, data.gbuf3 },
                                .depth = data.depth
                        }});

                EXPECT_FALSE(fg.isValid(data.depth));

                data.depth = rt.attachments.depth;
                data.gbuf1 = rt.attachments.color[0];
                data.gbuf2 = rt.attachments.color[1];
                data.gbuf3 = rt.attachments.color[2];
            },
            [=](FrameGraphResources const& resources, auto const& data, backend::DriverApi& driver) {
                Texture const& depth = resources.get(data.depth);
                Texture const& gbuf1 = resources.get(data.gbuf1);
                Texture const& gbuf2 = resources.get(data.gbuf2);
                Texture const& gbuf3 = resources.get(data.gbuf3);
                EXPECT_TRUE((bool)depth.texture);
                EXPECT_TRUE((bool)gbuf1.texture);
                EXPECT_TRUE((bool)gbuf2.texture);
                EXPECT_TRUE((bool)gbuf3.texture);
                auto rp = resources.getRenderPassInfo();
                EXPECT_EQ(rp.params.flags.discardStart,
                        TargetBufferFlags::COLOR0
                        | TargetBufferFlags::COLOR1
                        | TargetBufferFlags::COLOR2);
                EXPECT_EQ(rp.params.flags.discardEnd, TargetBufferFlags::COLOR0);
                EXPECT_EQ(rp.params.viewport.width, 16);
                EXPECT_EQ(rp.params.viewport.height, 32);
                EXPECT_TRUE((bool)rp.target);
            });

    struct LightingPassData {
        FrameGraphId<Texture> lightingBuffer;
        FrameGraphId<Texture> depth;
        FrameGraphId<Texture> gbuf1;
        FrameGraphId<Texture> gbuf2;
        FrameGraphId<Texture> gbuf3;
    };
    auto& lightingPass = fg.addPass<LightingPassData>("Lighting pass",
            [&](FrameGraph::Builder& builder, auto& data) {
                data.depth = builder.read(gBufferPass->depth, Texture::Usage::SAMPLEABLE);
                data.gbuf1 = gBufferPass->gbuf1;
                data.gbuf2 = builder.read(gBufferPass->gbuf2, Texture::Usage::SAMPLEABLE);
                data.gbuf3 = builder.read(gBufferPass->gbuf3, Texture::Usage::SAMPLEABLE);
                Texture::Descriptor desc = builder.getDescriptor(data.depth);
                data.lightingBuffer = builder.create<Texture>("Lighting buffer", desc);
                builder.useAsRenderTarget(&data.lightingBuffer);
            },
            [=](FrameGraphResources const& resources, auto const& data, backend::DriverApi& driver) {
                Texture const& lightingBuffer = resources.get(data.lightingBuffer);
                Texture const& depth = resources.get(data.depth);
                Texture const& gbuf1 = resources.get(data.gbuf1);
                Texture const& gbuf2 = resources.get(data.gbuf2);
                Texture const& gbuf3 = resources.get(data.gbuf3);
                EXPECT_TRUE((bool)lightingBuffer.texture);
                EXPECT_TRUE((bool)depth.texture);
                EXPECT_FALSE((bool)gbuf1.texture);
                EXPECT_TRUE((bool)gbuf2.texture);
                EXPECT_TRUE((bool)gbuf3.texture);
                auto rp = resources.getRenderPassInfo();
                EXPECT_EQ(rp.params.flags.discardStart,TargetBufferFlags::COLOR0);
                EXPECT_EQ(rp.params.flags.discardEnd, TargetBufferFlags::NONE);
                EXPECT_EQ(rp.params.viewport.width, 16);
                EXPECT_EQ(rp.params.viewport.height, 32);
                EXPECT_TRUE((bool)rp.target);
            });

    struct DebugPass {
        FrameGraphId<Texture> debugBuffer;
        FrameGraphId<Texture> gbuf1;
        FrameGraphId<Texture> gbuf2;
        FrameGraphId<Texture> gbuf3;
    };
    auto& culledPass = fg.addPass<DebugPass>("DebugPass pass",
            [&](FrameGraph::Builder& builder, auto& data) {
                data.gbuf1 = builder.read(lightingPass->gbuf1, Texture::Usage::SAMPLEABLE);
                data.gbuf2 = builder.read(lightingPass->gbuf2, Texture::Usage::SAMPLEABLE);
                data.gbuf3 = builder.read(lightingPass->gbuf3, Texture::Usage::SAMPLEABLE);
                Texture::Descriptor desc = builder.getDescriptor(data.gbuf1);
                data.debugBuffer = builder.create<Texture>("Debug buffer", desc);
                builder.useAsRenderTarget(&data.debugBuffer);
            },
            [=](FrameGraphResources const& resources, auto const& data, backend::DriverApi& driver) {
                Texture const& debugBuffer = resources.get(data.debugBuffer);
                Texture const& gbuf1 = resources.get(data.gbuf1);
                Texture const& gbuf2 = resources.get(data.gbuf2);
                Texture const& gbuf3 = resources.get(data.gbuf3);
                EXPECT_FALSE((bool)debugBuffer.texture);
                EXPECT_TRUE((bool)gbuf1.texture);
                EXPECT_TRUE((bool)gbuf2.texture);
                EXPECT_TRUE((bool)gbuf3.texture);
                auto rp = resources.getRenderPassInfo();
                EXPECT_FALSE((bool)rp.target);
            });

    struct PostPassData {
        FrameGraphId<Texture> lightingBuffer;
        FrameGraphId<Texture> backBuffer;
        struct {
            FrameGraphId<Texture> depth;
            FrameGraphId<Texture> gbuf1;
            FrameGraphId<Texture> gbuf2;
            FrameGraphId<Texture> gbuf3;
        } destroyed;
    };
    auto& postPass = fg.addPass<PostPassData>("Post pass",
            [&](FrameGraph::Builder& builder, auto& data) {
                data.lightingBuffer = builder.read(lightingPass->lightingBuffer, Texture::Usage::SAMPLEABLE);
                Texture::Descriptor desc = builder.getDescriptor(data.lightingBuffer);
                data.backBuffer = builder.create<Texture>("Backbuffer", desc);
                builder.useAsRenderTarget(&data.backBuffer);
                data.destroyed.depth = lightingPass->depth;
                data.destroyed.gbuf1 = lightingPass->gbuf1;
                data.destroyed.gbuf2 = lightingPass->gbuf2;
                data.destroyed.gbuf3 = lightingPass->gbuf3;
            },
            [=](FrameGraphResources const& resources, auto const& data, backend::DriverApi& driver) {
                Texture const& lightingBuffer = resources.get(data.lightingBuffer);
                Texture const& backBuffer = resources.get(data.backBuffer);
                EXPECT_TRUE((bool)lightingBuffer.texture);
                EXPECT_TRUE((bool)backBuffer.texture);
                EXPECT_FALSE((bool)resources.get(data.destroyed.depth).texture);
                EXPECT_FALSE((bool)resources.get(data.destroyed.gbuf1).texture);
                EXPECT_FALSE((bool)resources.get(data.destroyed.gbuf2).texture);
                EXPECT_FALSE((bool)resources.get(data.destroyed.gbuf3).texture);

                EXPECT_EQ(resources.getUsage(data.lightingBuffer),  Texture::Usage::SAMPLEABLE | Texture::Usage::COLOR_ATTACHMENT);
                EXPECT_EQ(resources.getUsage(data.backBuffer),                                   Texture::Usage::COLOR_ATTACHMENT);
                EXPECT_EQ(resources.getUsage(data.destroyed.depth), Texture::Usage::SAMPLEABLE | Texture::Usage::DEPTH_ATTACHMENT);
                EXPECT_EQ(resources.getUsage(data.destroyed.gbuf1),                              Texture::Usage::COLOR_ATTACHMENT);
                EXPECT_EQ(resources.getUsage(data.destroyed.gbuf2), Texture::Usage::SAMPLEABLE | Texture::Usage::COLOR_ATTACHMENT);
                EXPECT_EQ(resources.getUsage(data.destroyed.gbuf3), Texture::Usage::SAMPLEABLE | Texture::Usage::COLOR_ATTACHMENT);

                auto rp = resources.getRenderPassInfo();
                EXPECT_EQ(rp.params.flags.discardStart,TargetBufferFlags::COLOR0);
                EXPECT_EQ(rp.params.flags.discardEnd, TargetBufferFlags::NONE);
                EXPECT_EQ(rp.params.viewport.width, 16);
                EXPECT_EQ(rp.params.viewport.height, 32);
                EXPECT_TRUE((bool)rp.target);
            });

    fg.present(postPass->backBuffer);
    fg.compile();
    fg.execute(driverApi);
}

TEST_F(FrameGraphTest, ImportResource) {
    Texture outputTexture{ .texture = Handle<HwTexture>{ 0x1234 }};
    FrameGraphId<Texture> output = fg.import("Imported Texture", Texture::Descriptor{
            .width = 320,
            .height = 200
    }, outputTexture);

    EXPECT_TRUE(fg.isValid(output));

    struct PassData {
        FrameGraphId<Texture> output;
    };
    auto& pass = fg.addPass<PassData>("Pass", [&](FrameGraph::Builder& builder, auto& data) {
                auto const& desc = builder.getDescriptor(output);
                EXPECT_EQ(desc.width, 320);
                EXPECT_EQ(desc.height, 200);

                data.output = builder.write(output, Texture::Usage::COLOR_ATTACHMENT);
                EXPECT_TRUE(fg.isValid(output)); // output is expect valid here because we never read before
                EXPECT_TRUE(fg.isValid(data.output));
            },
            [=](FrameGraphResources const& resources, auto const& data, backend::DriverApi& driver) {
                Texture const& texture = resources.get(data.output);
                EXPECT_EQ(texture.texture.getId(), 0x1234);
            });

    fg.present(pass->output);
    fg.compile();
    fg.execute(driverApi);
}
