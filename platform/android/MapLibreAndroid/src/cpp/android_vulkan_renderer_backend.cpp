#include "android_vulkan_renderer_backend.hpp"

#include <mbgl/gfx/backend_scope.hpp>
#include <mbgl/vulkan/context.hpp>
#include <mbgl/vulkan/renderable_resource.hpp>

#include <cassert>
#include <vulkan/vulkan_android.h>

#define ENABLE_SWAPPY

#ifdef ENABLE_SWAPPY
#include <swappy/swappyVk.h>
#include <jni/jni.hpp>
#endif

namespace mbgl {
namespace android {

#ifdef ENABLE_SWAPPY
extern JavaVM* _jvm;
#endif

class AndroidVulkanRenderableResource final : public mbgl::vulkan::SurfaceRenderableResource {
public:
    AndroidVulkanRenderableResource(AndroidVulkanRendererBackend& backend_, ANativeWindow* window_)
        : SurfaceRenderableResource(backend_),
          window(window_) {}

    std::vector<const char*> getDeviceExtensions() override {
        return {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };
    }

    void createPlatformSurface() override {
        const vk::AndroidSurfaceCreateInfoKHR createInfo({}, window);
        surface = backend.getInstance()->createAndroidSurfaceKHRUnique(createInfo, nullptr, backend.getDispatcher());

        const int apiLevel = android_get_device_api_level();
        if (apiLevel < __ANDROID_API_Q__) {
            setSurfaceTransformPollingInterval(30);
        }
    }

#ifdef ENABLE_SWAPPY
    ~AndroidVulkanRenderableResource() override { destroySwapchain(); }

    void initSwapchain(uint32_t w, uint32_t h) override {
        vulkan::SurfaceRenderableResource::initSwapchain(w, h);

        if (!surface) {
            return;
        }

        uint64_t refreshDuration; // nano
#if 1
        JNIEnv* env = nullptr;
        auto status = _jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            _jvm->AttachCurrentThread(&env, nullptr);
        }

        jclass activity = env->FindClass("org/maplibre/android/testapp/activity/maplayout/DebugModeActivity");
        jmethodID method = env->GetStaticMethodID(activity, "getThis", "()Landroid/app/Activity;");
        jobject jactivity = env->NewGlobalRef(env->CallStaticObjectMethod(activity, method));

        if (status == JNI_EDETACHED) {
            _jvm->DetachCurrentThread();
        }
#endif

        SwappyVk_setQueueFamilyIndex(backend.getDevice().get().operator VkDevice(),
                                     backend.getPresentQueue().operator VkQueue(),
                                     backend.getPresentQueueIndex());

        SwappyVk_initAndGetRefreshCycleDuration(env,
                                                jactivity,
                                                backend.getPhysicalDevice().operator VkPhysicalDevice(),
                                                backend.getDevice().get().operator VkDevice(),
                                                swapchain.get().operator VkSwapchainKHR(),
                                                &refreshDuration);

        SwappyVk_setWindow(
            backend.getDevice().get().operator VkDevice(), swapchain.get().operator VkSwapchainKHR(), window);

        // TODO query SwappyVk_getSupportedRefreshPeriodsNS()

        SwappyVk_setSwapIntervalNS(backend.getDevice().get().operator VkDevice(),
                                   swapchain.get().operator VkSwapchainKHR(),
                                   SWAPPY_SWAP_60FPS);
    }

    void destroySwapchain() override {
        if (surface) {
            backend.getDevice()->waitIdle(backend.getDispatcher());
            SwappyVk_destroySwapchain(backend.getDevice().get().operator VkDevice(),
                                      swapchain.get().operator VkSwapchainKHR());
        }

        vulkan::SurfaceRenderableResource::destroySwapchain();
    }

    // TODO this throws validation errors with current presentSemaphore[swapImageCount] and
    // double buffered frame resources
    vk::Result presentFrame(const vk::PresentInfoKHR& info) override {
        const auto pInfo = &info.operator const VkPresentInfoKHR&();
        return vk::Result(SwappyVk_queuePresent(backend.getPresentQueue().operator VkQueue(), pInfo));
    }
#endif

    void bind() override {}
    void swap() override {
        vulkan::SurfaceRenderableResource::swap();

        const auto& swapBehaviour = static_cast<AndroidVulkanRendererBackend&>(backend).getSwapBehavior();
        if (swapBehaviour == gfx::Renderable::SwapBehaviour::Flush) {
            static_cast<vulkan::Context&>(backend.getContext()).waitFrame();
        }
    }

private:
    ANativeWindow* window;
};

AndroidVulkanRendererBackend::AndroidVulkanRendererBackend(ANativeWindow* window_)
    : vulkan::RendererBackend(gfx::ContextMode::Unique),
      vulkan::Renderable({64, 64}, std::make_unique<AndroidVulkanRenderableResource>(*this, window_)) {
    init();
}

AndroidVulkanRendererBackend::~AndroidVulkanRendererBackend() = default;

std::vector<const char*> AndroidVulkanRendererBackend::getInstanceExtensions() {
    auto extensions = mbgl::vulkan::RendererBackend::getInstanceExtensions();
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
    return extensions;
}

void AndroidVulkanRendererBackend::resizeFramebuffer(int width, int height) {
    size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    if (context) {
        static_cast<vulkan::Context&>(*context).requestSurfaceUpdate();
    }
}

PremultipliedImage AndroidVulkanRendererBackend::readFramebuffer() {
    // TODO not implemented
    return PremultipliedImage(Size(2, 2));
}

} // namespace android
} // namespace mbgl

namespace mbgl {
namespace gfx {

template <>
std::unique_ptr<android::AndroidRendererBackend> Backend::Create<mbgl::gfx::Backend::Type::Vulkan>(
    ANativeWindow* window) {
    return std::make_unique<android::AndroidVulkanRendererBackend>(window);
}

} // namespace gfx
} // namespace mbgl
