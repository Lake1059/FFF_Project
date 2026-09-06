#include "pch.h"
#include "3FP/Render/VideoRenderer.h"

#include <cmath>
#include <iostream>

using Microsoft::WRL::ComPtr;

struct TimedTextAtlasRegression {
    static constexpr std::uint32_t Width = 1024;
    static constexpr std::uint32_t Height = 256;

    static void Require(bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    }

    static TimedTextRenderCommand Text(const wchar_t* text, float width = 120.0f) {
        TimedTextRenderCommand command;
        command.content = std::make_shared<TimedTextRenderCommand::TextContent>(
            TimedTextRenderCommand::TextContent{1, text, L"Arial"});
        command.contentId = 1;
        command.x = 100; command.y = 80;
        command.width = width; command.height = 80;
        command.fontSize = 64;
        command.foregroundArgb = 0xffffffff;
        command.outlineArgb = 0xff000000;
        return command;
    }

    static std::vector<std::uint8_t> Render(PlayerVideoRenderer& renderer,
        std::vector<TimedTextRenderCommand> commands, std::uint32_t canvasWidth = Width) {
        Require(renderer.EnsureDevice() == FFFResult::Success, "GPU device unavailable");
        renderer.swapWidth_ = Width; renderer.swapHeight_ = Height;
        auto layer = std::make_shared<TimedTextRenderLayer>();
        layer->canvasWidth = canvasWidth; layer->canvasHeight = Height;
        layer->sequence = renderer.timedTextRenderedSequences_[1] + 1;
        layer->commands = std::move(commands);
        renderer.timedTextLayers_[1] = layer;
        Require(renderer.DrawTimedText(TimedTextLayerSlot::Danmaku) == FFFResult::Success,
            "Production timed-text draw failed");
        D3D11_TEXTURE2D_DESC description{};
        renderer.timedTextTextures_[1]->GetDesc(&description);
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0; description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        Require(SUCCEEDED(renderer.device_->CreateTexture2D(&description, nullptr, &staging)),
            "Staging texture creation failed");
        renderer.context_->CopyResource(staging.Get(), renderer.timedTextTextures_[1]);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(SUCCEEDED(renderer.context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
            "GPU readback failed");
        const std::size_t pixelStride = description.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4;
        std::vector<std::uint8_t> pixels(Width * Height * pixelStride);
        for (std::size_t row = 0; row < Height; ++row)
            std::memcpy(pixels.data() + row * Width * pixelStride,
                static_cast<const std::uint8_t*>(mapped.pData) + row * mapped.RowPitch, Width * pixelStride);
        renderer.context_->Unmap(staging.Get(), 0);
        return pixels;
    }

    static std::size_t Difference(const std::vector<std::uint8_t>& expected,
        const std::vector<std::uint8_t>& actual, int tolerance = 1) {
        std::size_t differences = 0;
        for (std::size_t pixel = 0; pixel < expected.size(); pixel += 4) {
            for (std::size_t channel = 0; channel < 4; ++channel) {
                if (std::abs(static_cast<int>(expected[pixel + channel]) -
                    actual[pixel + channel]) > tolerance) {
                    if (differences < 4)
                        std::cout << "pixel " << (pixel / 4) % Width << "," << (pixel / 4) / Width
                            << " channel=" << channel << " expected=" << static_cast<int>(expected[pixel + channel])
                            << " actual=" << static_cast<int>(actual[pixel + channel]) << std::endl;
                    ++differences;
                    break;
                }
            }
        }
        return differences;
    }

    static std::size_t GutterPixels(PlayerVideoRenderer& renderer) {
        D3D11_TEXTURE2D_DESC description{};
        renderer.timedTextAtlasTexture_->GetDesc(&description);
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0; description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        Require(SUCCEEDED(renderer.device_->CreateTexture2D(&description, nullptr, &staging)),
            "Atlas staging texture failed");
        renderer.context_->CopyResource(staging.Get(), renderer.timedTextAtlasTexture_);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(SUCCEEDED(renderer.context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
            "Atlas readback failed");
        const std::size_t stride = description.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4;
        std::size_t occupied = 0;
        const auto inspect = [&](std::size_t column, std::size_t row) {
            const auto* pixel = static_cast<const std::uint8_t*>(mapped.pData) +
                row * mapped.RowPitch + column * stride;
            if (std::any_of(pixel, pixel + stride, [](std::uint8_t value) { return value != 0; }))
                ++occupied;
        };
        for (const auto& entry : renderer.timedTextSprites_) {
            const auto& sprite = entry.second;
            const auto left = static_cast<std::size_t>(sprite.atlasX);
            const auto top = static_cast<std::size_t>(sprite.atlasY);
            const auto right = left + static_cast<std::size_t>(sprite.width) - 1;
            const auto bottom = top + static_cast<std::size_t>(sprite.height) - 1;
            for (auto column = left; column <= right; ++column) {
                inspect(column, top); inspect(column, bottom);
            }
            for (auto row = top + 1; row < bottom; ++row) {
                inspect(left, row); inspect(right, row);
            }
        }
        renderer.context_->Unmap(staging.Get(), 0);
        return occupied;
    }

    static void Run() {
        std::size_t failures = 0;
        const auto check = [&](const char* name, std::size_t difference) {
            std::cout << name << ": " << difference << std::endl;
            if (difference != 0) ++failures;
        };
        for (const auto text : {L"ffffj", L"WWWWWWW", L"弹幕边缘测试j", L"Áj\u0301\u0323",
            L"\U0001F469\u200d\U0001F4BB \U0001F1E8\U0001F1F3 \u2615\ufe0f ij"}) {
            PlayerVideoRenderer cached, direct;
            auto command = Text(text);
            command.flags = static_cast<FFF3FPTimedTextFlags>(
                static_cast<std::uint32_t>(FFF3FPTimedTextFlags::Bold) |
                static_cast<std::uint32_t>(FFF3FPTimedTextFlags::Italic));
            command.outlineWidth = 1.25f;
            const auto actual = Render(cached, {command});
            command.contentId = 0;
            const auto expected = Render(direct, {command});
            check("ink bounds missing/extra pixels", Difference(expected, actual));
        }
        for (const auto outline : {0.0f, 0.5f, 4.0f, 12.0f}) {
            for (const auto flags : {0u, 3u, 15u}) {
                PlayerVideoRenderer cached, direct;
                auto command = Text(L"Áj\u0301\u0323 AVfj 下划线 العربية");
                command.flags = static_cast<FFF3FPTimedTextFlags>(flags);
                command.outlineWidth = outline;
                command.shadowArgb = 0x80203040;
                command.shadowOffsetX = -3; command.shadowOffsetY = 4;
                const auto actual = Render(cached, {command});
                command.contentId = 0;
                check("outline/decorations/hard-shadow", Difference(Render(direct, {command}), actual));
            }
        }
        {
            PlayerVideoRenderer cached, isolated;
            auto target = Text(L"f", 22);
            target.flags = FFF3FPTimedTextFlags::Italic;
            target.x = 100.25f;
            const auto before = Render(isolated, {target});
            auto neighbor = Text(L"WWWWWWW", 4);
            neighbor.x = -2000;
            neighbor.flags = FFF3FPTimedTextFlags::SoftShadow;
            neighbor.shadowArgb = 0xffff0000;
            neighbor.shadowOffsetX = neighbor.shadowOffsetY = 6;
            neighbor.outlineWidth = 4;
            Render(cached, {neighbor});
            const auto after = Render(cached, {target});
            check("neighbor shadow contamination", Difference(before, after, 0));
        }
        {
            PlayerVideoRenderer renderer;
            auto command = Text(L"fractional scrolling", 410.33f);
            Render(renderer, {command}, 777);
            const auto misses = renderer.timedTextSpriteCacheMisses_;
            for (std::uint32_t frame = 0; frame < 80; ++frame) {
                command.x = 10.17f + frame * 11.371f;
                Render(renderer, {command}, 777);
            }
            check("scrolling cache misses", renderer.timedTextSpriteCacheMisses_ - misses);
        }
        {
            PlayerVideoRenderer renderer;
            auto command = Text(L"Duplicate sprite");
            Render(renderer, std::vector<TimedTextRenderCommand>(80, command));
            check("duplicate sprite allocations", renderer.timedTextSpriteCacheMisses_ - 1);
        }
        for (const auto hdr : {false, true}) {
            PlayerVideoRenderer renderer, isolated;
            auto target = Text(L"f", 22);
            target.x = 100.25f;
            target.flags = static_cast<FFF3FPTimedTextFlags>(
                static_cast<std::uint32_t>(FFF3FPTimedTextFlags::Italic) |
                static_cast<std::uint32_t>(FFF3FPTimedTextFlags::SoftShadow));
            target.outlineWidth = 2;
            target.shadowArgb = 0xa0102030;
            target.shadowOffsetX = target.shadowOffsetY = 6;
            const auto expected = Render(isolated, {target});
            if (hdr) renderer.actualMode_ = FFF3FPColorMode::MapToHdr;
            for (std::uint32_t round = 0; round < 3; ++round) {
                std::vector<TimedTextRenderCommand> commands;
                for (std::uint32_t index = 0; index < 140; ++index) {
                    auto neighbor = Text((L"neighbor AVjf " + std::to_wstring(round * 140 + index)).c_str(), 3);
                    neighbor.x = -2000;
                    neighbor.flags = target.flags;
                    neighbor.fontSize = 16.0f + index % 6;
                    neighbor.outlineWidth = 0.5f + index % 3;
                    neighbor.shadowArgb = 0xff804020;
                    neighbor.shadowOffsetX = neighbor.shadowOffsetY = 0.75f + (index % 4) * 2;
                    commands.push_back(std::move(neighbor));
                }
                commands.insert(commands.begin() + 70, target);
                Render(renderer, std::move(commands));
                const auto actual = Render(renderer, {target});
                if (!hdr) check("dense mixed shadows", Difference(expected, actual, 2));
                check(hdr ? "HDR atlas transparent gutters" : "SDR atlas transparent gutters", GutterPixels(renderer));
            }
        }
        {
            PlayerVideoRenderer renderer;
            auto command = Text(L"WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", 10);
            command.flags = FFF3FPTimedTextFlags::SoftShadow;
            command.shadowArgb = 0xff000000;
            command.shadowOffsetX = command.shadowOffsetY = 4;
            Render(renderer, {command});
            check("long sprite missing from atlas", renderer.timedTextSprites_.empty() ? 1 : 0);
            check("long sprite did not grow atlas", renderer.timedTextAtlasSize_ <= 1024 ? 1 : 0);
        }
        {
            PlayerVideoRenderer renderer;
            std::vector<TimedTextRenderCommand> commands;
            for (std::uint32_t index = 0; index < 512; ++index) {
                auto command = Text((L"old layout " + std::to_wstring(index)).c_str());
                command.contentId = 0; command.fontSize = 12; command.x = -2000;
                commands.push_back(std::move(command));
            }
            Render(renderer, commands);
            commands.front().contentId = 1;
            for (std::size_t index = 1; index < commands.size(); ++index) {
                auto command = Text((L"new layout " + std::to_wstring(index)).c_str());
                command.fontSize = 12; command.x = -2000;
                commands[index] = std::move(command);
            }
            Render(renderer, std::move(commands));
            check("retained layouts across cache eviction", renderer.timedTextSprites_.size() == 512 ? 0 : 1);
            check("pending layouts released", renderer.timedTextPendingSprites_.size());
        }
        Require(failures == 0, "Timed-text GPU pixel regression failed");
    }
};

int main() {
    try {
        TimedTextAtlasRegression::Run();
        std::cout << "Timed-text GPU pixel regression passed." << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
