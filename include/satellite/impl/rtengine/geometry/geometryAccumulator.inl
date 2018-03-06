#pragma once

#include "../../../rtengine/geometry/geometryAccumulator.hpp"

namespace NSM
{
    namespace rt
    {

        void GeometryAccumulator::init(DeviceQueueType &device)
        {
            this->device = device;

            std::vector<vk::DescriptorSetLayoutBinding> loaderDescriptorSetBindings = {
                // vertex inputs
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // counters
                vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // buffer data space
                vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // buffer regions
                vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // buffer views
                vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // data formats
                vk::DescriptorSetLayoutBinding(5, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // data bindings (with buffer views)
                vk::DescriptorSetLayoutBinding(6, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // mesh uniforms

                // write buffers
                vk::DescriptorSetLayoutBinding(7, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // material buffer
                vk::DescriptorSetLayoutBinding(8, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // order buffer
                vk::DescriptorSetLayoutBinding(9, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // vertex linear buffer

                // write images with vertex data
                vk::DescriptorSetLayoutBinding(10, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // attributed data (alpha footage)
            };

            // loader descriptor layout
            loaderDescriptorLayout = std::vector<vk::DescriptorSetLayout>{ device->logical.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo().setPBindings(loaderDescriptorSetBindings.data()).setBindingCount(loaderDescriptorSetBindings.size())) };
            loaderDescriptorSets = device->logical.allocateDescriptorSets(vk::DescriptorSetAllocateInfo().setDescriptorPool(device->descriptorPool).setDescriptorSetCount(loaderDescriptorLayout.size()).setPSetLayouts(loaderDescriptorLayout.data()));

            // create pipeline layout and caches
            pipelineLayout = device->logical.createPipelineLayout(vk::PipelineLayoutCreateInfo().setPSetLayouts(loaderDescriptorLayout.data()).setSetLayoutCount(loaderDescriptorLayout.size()));
            pipelineCache = device->logical.createPipelineCache(vk::PipelineCacheCreateInfo());

            // vertex loader
            geometryLoader.pipeline = createCompute(device, shadersPathPrefix + "/vertex/loader.comp.spv", pipelineLayout, pipelineCache);

            // loading geometry command
            geometryLoader.dispatch = [&]() {
                auto commandBuffer = getCommandBuffer(device, true);
                commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, loaderDescriptorSets, nullptr);
                commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, geometryLoader.pipeline);
                commandBuffer.dispatch(INTENSIVITY, 1, 1);
                flushCommandBuffer(device, commandBuffer, true);
            };

            // recommended alloc 256Mb for all staging
            // but here can be used 4Kb
            generalStagingBuffer = createBuffer(device, strided<uint32_t>(1024), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
            generalLoadingBuffer = createBuffer(device, strided<uint32_t>(1024), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_GPU_TO_CPU);

            // create didcated counter for accumulator
            geometryCounter = createBuffer(device, strided<uint32_t>(2), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_GPU_ONLY);

            // descriptor templates
            auto desc0Tmpl = vk::WriteDescriptorSet().setDstSet(loaderDescriptorSets[0]).setDstArrayElement(0).setDescriptorCount(1).setDescriptorType(vk::DescriptorType::eStorageBuffer);
            device->logical.updateDescriptorSets(std::vector<vk::WriteDescriptorSet>{
                vk::WriteDescriptorSet(desc0Tmpl).setDescriptorType(vk::DescriptorType::eStorageBuffer).setDstBinding(0).setPBufferInfo(&geometryCounter->descriptorInfo),
            },
                nullptr);

            {
                zerosBufferReference = createBuffer(device, strided<uint32_t>(1024), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
                debugOnes32BufferReference = createBuffer(device, strided<uint32_t>(1024), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);

                // minmaxes
                std::vector<glm::vec4> minmaxes(64);
                for (int i = 0; i < 32; i++)
                {
                    minmaxes[i * 2 + 0] = glm::vec4(10000.f), minmaxes[i * 2 + 1] = glm::vec4(-10000.f);
                }

                // zeros
                std::vector<uint32_t> zeros(1024);
                std::vector<uint32_t> ones(1024);
                for (int i = 0; i < 1024; i++)
                {
                    zeros[i] = 0, ones[i] = 1;
                }

                // make reference buffers
                auto command = getCommandBuffer(device, true);
                bufferSubData(command, zerosBufferReference, zeros, 0); // make reference of zeros
                bufferSubData(command, debugOnes32BufferReference, ones, 0);
                flushCommandBuffer(device, command, true);
            }

            resetAccumulationCounter();
        }

        void GeometryAccumulator::resetAccumulationCounter()
        {
            flushCommandBuffer(device, createCopyCmd<BufferType &, BufferType &, vk::BufferCopy>(device, zerosBufferReference, geometryCounter, { 0, 0, strided<uint32_t>(2) }), true);
        }

        void GeometryAccumulator::allocatePrimitiveReserve(size_t primitiveCount)
        {
            // sideloader
            size_t _MAX_HEIGHT = (primitiveCount > 0ll ? (primitiveCount * ATTRIBUTE_EXTENT - 1ll) / WARPED_WIDTH + 1ll : 0ll) + 1ll; // special values
            attributeTexelWorking = createTexture(device, vk::ImageViewType::e2D, vk::Extent3D{ uint32_t(_WIDTH), uint32_t(_MAX_HEIGHT), 1 }, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc, vk::Format::eR32G32B32A32Sfloat);
            materialIndicesWorking = createBuffer(device, strided<uint32_t>(primitiveCount * 2), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_GPU_ONLY);
            orderIndicesWorking = createBuffer(device, strided<uint32_t>(primitiveCount * 2), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_GPU_ONLY);
            vertexLinearWorking = createBuffer(device, strided<float>(primitiveCount * 2 * 9), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_GPU_ONLY);

            // descriptor templates
            auto desc0Tmpl = vk::WriteDescriptorSet().setDstSet(loaderDescriptorSets[0]).setDstArrayElement(0).setDescriptorCount(1).setDescriptorType(vk::DescriptorType::eStorageBuffer);
            device->logical.updateDescriptorSets(std::vector<vk::WriteDescriptorSet>{
                vk::WriteDescriptorSet(desc0Tmpl).setDescriptorType(vk::DescriptorType::eStorageImage).setDstBinding(10).setPImageInfo(&attributeTexelWorking->descriptorInfo),
                    vk::WriteDescriptorSet(desc0Tmpl).setDescriptorType(vk::DescriptorType::eStorageBuffer).setDstBinding(7).setPBufferInfo(&materialIndicesWorking->descriptorInfo),
                    vk::WriteDescriptorSet(desc0Tmpl).setDescriptorType(vk::DescriptorType::eStorageBuffer).setDstBinding(8).setPBufferInfo(&orderIndicesWorking->descriptorInfo),
                    vk::WriteDescriptorSet(desc0Tmpl).setDescriptorType(vk::DescriptorType::eStorageBuffer).setDstBinding(9).setPBufferInfo(&vertexLinearWorking->descriptorInfo),
            },
                nullptr);
        }

        void GeometryAccumulator::pushGeometry(std::shared_ptr<VertexInstance> &vertexInstance)
        {
            // write mesh buffers to loaders descriptors
            auto desc0Tmpl = vk::WriteDescriptorSet().setDstSet(loaderDescriptorSets[0]).setDstArrayElement(0).setDescriptorCount(1).setDescriptorType(vk::DescriptorType::eStorageBuffer);
            device->logical.updateDescriptorSets(std::vector<vk::WriteDescriptorSet>{
                vk::WriteDescriptorSet(desc0Tmpl).setDstBinding(1).setPBufferInfo(&vertexInstance->getBufferSpaceBuffer()->descriptorInfo),
                    vk::WriteDescriptorSet(desc0Tmpl).setDstBinding(2).setPBufferInfo(&vertexInstance->getBufferSpaceRegions()->descriptorInfo),
                    vk::WriteDescriptorSet(desc0Tmpl).setDstBinding(3).setPBufferInfo(&vertexInstance->getBufferViewsBuffer()->descriptorInfo),
                    vk::WriteDescriptorSet(desc0Tmpl).setDstBinding(4).setPBufferInfo(&vertexInstance->getDataFormatBuffer()->descriptorInfo),
                    vk::WriteDescriptorSet(desc0Tmpl).setDstBinding(5).setPBufferInfo(&vertexInstance->getBufferBindingBuffer()->descriptorInfo),
                    vk::WriteDescriptorSet(desc0Tmpl).setDstBinding(6).setPBufferInfo(&vertexInstance->getUniformBuffer()->descriptorInfo),
            },
                nullptr);

            // dispatch loading
            geometryLoader.dispatch();
        }
    }
} // namespace NSM

