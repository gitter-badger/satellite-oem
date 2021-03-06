#pragma once

#include "../../rtengine/textureSet.hpp"

namespace NSM {
    namespace rt {

        void TextureSet::init(DeviceQueueType& device) {
            this->device = device;

            textures = std::vector<TextureType>(0);
            freedomTextures = std::vector<size_t>(0);
        }

        TextureSet::TextureSet(TextureSet& another) {
            device = another.device;
            freedomTextures = another.freedomTextures;
            textures = another.textures;
        }

        TextureSet::TextureSet(TextureSet&& another) {
            device = std::move(another.device);
            freedomTextures = std::move(another.freedomTextures);
            textures = std::move(another.textures);
        }

        void TextureSet::freeTexture(const int32_t& idx) {
            freedomTextures.push_back(idx);
        }

        void TextureSet::clearTextures() {
            freedomTextures.resize(0);
            textures.resize(0);
        }

        void TextureSet::setTexture(const int32_t &idx, const TextureType& texture) {
            for (int i = 0; i < freedomTextures.size(); i++) {
                if (freedomTextures[i] == idx) freedomTextures.erase(freedomTextures.begin() + i);
            }
            if (textures.size() <= idx) textures.resize(idx+1);
            textures[idx] = texture;
        }

        int32_t TextureSet::loadTexture(const TextureType& texture) {
            int32_t idx = -1;
            if (freedomTextures.size() > 0) {
                idx = freedomTextures[freedomTextures.size() - 1];
                freedomTextures.pop_back();
                textures[idx] = texture;
            } else {
                idx = textures.size();
                textures.push_back(texture);
            }
            return idx;
        };

        bool TextureSet::haveTextures() {
            return textures.size() > 0;
        }

        std::vector<TextureType>& TextureSet::getTextures() {
            return textures;
        }

#ifdef USE_CIMG
        int32_t TextureSet::loadTexture(std::string tex, bool force_write) {
            cil::CImg<uint8_t> image(tex.c_str());
            uint32_t width = image.width(), height = image.height(), spectrum = image.spectrum();
            image.channels(0, 3);
            if (spectrum == 3) image.get_shared_channel(3).fill(255); // if RGB, will alpha channel
            image.permute_axes("cxyz");


            // create texture
            auto texture = createTexture(device, vk::ImageViewType::e2D, { width, height, 1 }, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc, vk::Format::eR8G8B8A8Unorm, 1);
            auto tstage = createBuffer(device, image.size() * sizeof(uint8_t), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eStorageTexelBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);

            // purple-black square
            {
                auto command = getCommandBuffer(device, true);
                bufferSubData(command, tstage, (const uint8_t *)image.data(), image.size() * sizeof(uint8_t), 0);
                memoryCopyCmd(command, tstage, texture, vk::BufferImageCopy()
                    .setImageExtent({ width, height, 1 })
                    .setImageOffset({ 0, 0, 0 })
                    .setBufferOffset(0)
                    .setBufferRowLength(width)
                    .setBufferImageHeight(height)
                    .setImageSubresource(texture->subresourceLayers));
                flushCommandBuffer(device, command, [&]() { destroyBuffer(tstage); });
            }

            return this->loadTexture(texture);
        }
#endif

    }
}