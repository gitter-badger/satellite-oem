#pragma once

#include "../../../rtengine/geometry/geometryAccumulator.hpp"

namespace NSM
{
    namespace rt
    {

        void GeometryAccumulator::init(DeviceQueueType &_device)
        {
            this->device = _device;

            std::vector<vk::DescriptorSetLayoutBinding> loaderDescriptorSetBindings = {
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // counters
                vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // material buffer
                vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // order buffer
                vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // vertex linear buffer
                vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // attributed data (alpha footage)
            };

            // from vertex instance
            std::vector<vk::DescriptorSetLayoutBinding> vertexInstanceDescreiptorBindings = {
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // buffer data space
                vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // buffer regions
                vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // buffer views
                vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // data formats
                vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // data bindings (with buffer views)
                vk::DescriptorSetLayoutBinding(5, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr), // mesh uniforms
            };


            // loader descriptor layout
            loaderDescriptorLayout = std::vector<vk::DescriptorSetLayout>{ 
                device->logical.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo().setPBindings(loaderDescriptorSetBindings.data()).setBindingCount(loaderDescriptorSetBindings.size())),
                device->logical.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo().setPBindings(vertexInstanceDescreiptorBindings.data()).setBindingCount(vertexInstanceDescreiptorBindings.size()))
            };
            loaderDescriptorSets = device->logical.allocateDescriptorSets(vk::DescriptorSetAllocateInfo().setDescriptorPool(device->descriptorPool).setDescriptorSetCount(loaderDescriptorLayout.size()).setPSetLayouts(loaderDescriptorLayout.data()));

            // create pipeline layout and caches
            pipelineLayout = device->logical.createPipelineLayout(vk::PipelineLayoutCreateInfo().setPSetLayouts(loaderDescriptorLayout.data()).setSetLayoutCount(loaderDescriptorLayout.size()));
            pipelineCache = device->logical.createPipelineCache(vk::PipelineCacheCreateInfo());

            {
                std::vector<vk::DescriptorUpdateTemplateEntry> entries = {
                    vk::DescriptorUpdateTemplateEntry().setDescriptorCount(1).setDescriptorType(vk::DescriptorType::eStorageBuffer).setDstArrayElement(0).setDstBinding(0).setOffset(offsetof(VertexInstanceViews, vInstanceBufferInfos[0])).setStride(sizeof(vk::DescriptorBufferInfo)),
                    vk::DescriptorUpdateTemplateEntry().setDescriptorCount(1).setDescriptorType(vk::DescriptorType::eStorageBuffer).setDstArrayElement(0).setDstBinding(1).setOffset(offsetof(VertexInstanceViews, vInstanceBufferInfos[1])).setStride(sizeof(vk::DescriptorBufferInfo)),
                    vk::DescriptorUpdateTemplateEntry().setDescriptorCount(1).setDescriptorType(vk::DescriptorType::eStorageBuffer).setDstArrayElement(0).setDstBinding(2).setOffset(offsetof(VertexInstanceViews, vInstanceBufferInfos[2])).setStride(sizeof(vk::DescriptorBufferInfo)),
                    vk::DescriptorUpdateTemplateEntry().setDescriptorCount(1).setDescriptorType(vk::DescriptorType::eStorageBuffer).setDstArrayElement(0).setDstBinding(3).setOffset(offsetof(VertexInstanceViews, vInstanceBufferInfos[3])).setStride(sizeof(vk::DescriptorBufferInfo)),
                    vk::DescriptorUpdateTemplateEntry().setDescriptorCount(1).setDescriptorType(vk::DescriptorType::eStorageBuffer).setDstArrayElement(0).setDstBinding(4).setOffset(offsetof(VertexInstanceViews, vInstanceBufferInfos[4])).setStride(sizeof(vk::DescriptorBufferInfo)),
                    vk::DescriptorUpdateTemplateEntry().setDescriptorCount(1).setDescriptorType(vk::DescriptorType::eStorageBuffer).setDstArrayElement(0).setDstBinding(5).setOffset(offsetof(VertexInstanceViews, vInstanceBufferInfos[5])).setStride(sizeof(vk::DescriptorBufferInfo))
                };

                descriptorVInstanceUpdateTemplate = device->logical.createDescriptorUpdateTemplateKHR(vk::DescriptorUpdateTemplateCreateInfo()
                    .setDescriptorSetLayout(loaderDescriptorLayout[1])
                    .setPDescriptorUpdateEntries(entries.data()).setDescriptorUpdateEntryCount(entries.size())
                    .setTemplateType(vk::DescriptorUpdateTemplateType::eDescriptorSet), nullptr, device->dldid);
            }

            // vertex loader
            geometryLoader.pipeline = createCompute(device, shadersPathPrefix + "/vertex/loader.comp.spv", pipelineLayout, pipelineCache);

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
            }, nullptr);

            {
                zerosBufferReference = createBuffer(device, strided<uint32_t>(1024), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
                debugOnes32BufferReference = createBuffer(device, strided<uint32_t>(1024), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);

                // minmaxes
                std::vector<bbox> minmaxes(32);
                std::for_each(std::execution::par_unseq, minmaxes.begin(), minmaxes.end(), [&](auto&& m) { m.mn = glm::vec4(10000.f), m.mx = glm::vec4(-10000.f); });

                // zeros and ones
                std::vector<uint32_t> zeros(1024), ones(1024);
                std::for_each(std::execution::par_unseq, zeros.begin(), zeros.end(), [&](auto&& m) { m = 0u; });
                std::for_each(std::execution::par_unseq, ones.begin(), ones.end(), [&](auto&& m) { m = 1u; });

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
                vk::WriteDescriptorSet(desc0Tmpl).setDescriptorType(vk::DescriptorType::eStorageBuffer).setDstBinding(1).setPBufferInfo(&materialIndicesWorking->descriptorInfo),
                vk::WriteDescriptorSet(desc0Tmpl).setDescriptorType(vk::DescriptorType::eStorageBuffer).setDstBinding(2).setPBufferInfo(&orderIndicesWorking->descriptorInfo),
                vk::WriteDescriptorSet(desc0Tmpl).setDescriptorType(vk::DescriptorType::eStorageBuffer).setDstBinding(3).setPBufferInfo(&vertexLinearWorking->descriptorInfo),
                vk::WriteDescriptorSet(desc0Tmpl).setDescriptorType(vk::DescriptorType::eStorageImage).setDstBinding(4).setPImageInfo(&attributeTexelWorking->descriptorInfo),
            }, nullptr);
        }

        void GeometryAccumulator::pushGeometry(std::shared_ptr<VertexInstance> vertexInstance, bool needUpdateDescriptor) {
            device->logical.updateDescriptorSetWithTemplateKHR(loaderDescriptorSets[1], descriptorVInstanceUpdateTemplate, &vertexInstance->getDescViewData(needUpdateDescriptor), device->dldid);

            auto commandBuffer = getCommandBuffer(device, true);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, loaderDescriptorSets, nullptr);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, geometryLoader.pipeline);
            commandBuffer.dispatch(INTENSIVITY, 1, 1);
            flushCommandBuffer(device, commandBuffer, true);
        }

    }
} // namespace NSM

