#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
void DrawEditor(const fs::path& directory, ImTextureID atlas);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static UINT width, height;

static LRESULT CALLBACK HandleMessage(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(window, message, w, l)) return 1;
    if (message == WM_SIZE && w != SIZE_MINIMIZED) {
        width = LOWORD(l);
        height = HIWORD(l);
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

static ComPtr<ID3D11ShaderResourceView> LoadAtlas(ID3D11Device* device, const fs::path& path) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    ComPtr<ID3D11ShaderResourceView> view;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return view;

    UINT w = 0, h = 0;
    if (FAILED(converter->GetSize(&w, &h)) || !w || !h || w > 4096 || h > 4096) return view;
    std::vector<BYTE> pixels(w * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return view;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels.data(), w * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    if (SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)))
        device->CreateShaderResourceView(texture.Get(), nullptr, &view);
    return view;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const WNDCLASSW wc{CS_HREDRAW | CS_VREDRAW, HandleMessage, 0, 0, instance,
        nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"ClassiCraftStudio"};
    RegisterClassW(&wc);
    HWND window = CreateWindowW(wc.lpszClassName, L"ClassiCraft Studio", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;

    DXGI_SWAP_CHAIN_DESC swapDesc{};
    swapDesc.BufferCount = 2;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.OutputWindow = window;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    for (auto driver : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &swapDesc, &swap, &device, nullptr, &context))) break;
    }
    if (!swap) {
        MessageBoxW(window, L"Direct3D 11 initialization failed.", L"ClassiCraft Studio", MB_ICONERROR);
        DestroyWindow(window);
        return 1;
    }

    wchar_t executable[32768];
    GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    const auto directory = fs::path(executable).parent_path();
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto atlas = LoadAtlas(device.Get(), directory / "assets" / "prototype-icons.png");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowPadding = {16, 14};
    ImGui::GetStyle().FramePadding = {8, 5};
    ImGui::GetStyle().FrameRounding = 4;
    ImGui::GetStyle().Colors[ImGuiCol_Header] = {0.19f, 0.29f, 0.43f, 1};
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(device.Get(), context.Get());
    ShowWindow(window, SW_SHOWDEFAULT);

    ComPtr<ID3D11RenderTargetView> target;
    bool running = true;
    while (running) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;
        if (IsIconic(window)) { WaitMessage(); continue; }
        if (width && height) {
            context->OMSetRenderTargets(0, nullptr, nullptr);
            target.Reset();
            if (FAILED(swap->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) break;
            width = height = 0;
        }
        if (!target) {
            ComPtr<ID3D11Texture2D> buffer;
            if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&buffer))) ||
                FAILED(device->CreateRenderTargetView(buffer.Get(), nullptr, &target))) break;
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawEditor(directory, reinterpret_cast<ImTextureID>(atlas.Get()));
        ImGui::Render();
        const float background[]{0.055f, 0.07f, 0.10f, 1};
        context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
        context->ClearRenderTargetView(target.Get(), background);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (FAILED(swap->Present(1, 0))) break;
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    atlas.Reset();
    if (SUCCEEDED(com)) CoUninitialize();
    if (IsWindow(window)) DestroyWindow(window);
    return 0;
}
