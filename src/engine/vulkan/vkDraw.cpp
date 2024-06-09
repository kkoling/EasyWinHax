#include "vkDraw.h"
#include "..\Engine.h"
#include "..\Vertex.h"
#include "..\..\proc.h"
#include "..\..\hooks\TrampHook.h"

namespace hax {

	namespace vk {

		static hax::in::TrampHook* pAcquireHook;
		static HANDLE hSemaphore;
		static VkDevice hDevice;

		static VkResult VKAPI_CALL hkvkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) {
			hDevice = device;
			pAcquireHook->disable();
			const PFN_vkAcquireNextImageKHR pAcquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(pAcquireHook->getOrigin());
			ReleaseSemaphore(hSemaphore, 1, nullptr);

			return pAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
		}


		static VkInstance createInstance(HMODULE hVulkan);
		static VkPhysicalDevice getPhysicalDevice(HMODULE hVulkan, VkInstance hInstance);
		static VkDevice createDummyDevice(HMODULE hVulkan, VkPhysicalDevice hPhysicalDevice);

		bool getVulkanInitData(VulkanInitData* initData) {
			const HMODULE hVulkan = hax::proc::in::getModuleHandle("vulkan-1.dll");

			if (!hVulkan) return false;

			const PFN_vkDestroyDevice pVkDestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(proc::in::getProcAddress(hVulkan, "vkDestroyDevice"));
			const PFN_vkDestroyInstance pVkDestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(proc::in::getProcAddress(hVulkan, "vkDestroyInstance"));

			if (!pVkDestroyDevice || !pVkDestroyInstance) return false;

			const VkInstance hInstance = createInstance(hVulkan);

			if (hInstance == VK_NULL_HANDLE) return false;

			VkPhysicalDevice hPhysicalDevice = getPhysicalDevice(hVulkan, hInstance);

			if (hPhysicalDevice == VK_NULL_HANDLE) return false;

			const VkDevice hDummyDevice = createDummyDevice(hVulkan, hPhysicalDevice);

			if (hDummyDevice == VK_NULL_HANDLE) return false;

			PFN_vkGetDeviceProcAddr pVkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(proc::in::getProcAddress(hVulkan, "vkGetDeviceProcAddr"));

			if (!pVkGetDeviceProcAddr) return false;

			initData->pVkQueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(pVkGetDeviceProcAddr(hDummyDevice, "vkQueuePresentKHR"));

			PFN_vkAcquireNextImageKHR pVkAcquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(pVkGetDeviceProcAddr(hDummyDevice, "vkAcquireNextImageKHR"));

			hSemaphore = CreateSemaphoreA(nullptr, 0, 1, nullptr);

			if (hSemaphore) {
				pAcquireHook = new hax::in::TrampHook(reinterpret_cast<BYTE*>(pVkAcquireNextImageKHR), reinterpret_cast<BYTE*>(hkvkAcquireNextImageKHR), 0xC);
				pAcquireHook->enable();
				WaitForSingleObject(hSemaphore, INFINITE);
				delete pAcquireHook;
				initData->hDevice = hDevice;
			}

			return true;
		}


		static VkInstance createInstance(HMODULE hVulkan) {
			const PFN_vkCreateInstance pVkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(proc::in::getProcAddress(hVulkan, "vkCreateInstance"));

			if (!pVkCreateInstance) return VK_NULL_HANDLE;

			constexpr const char* EXTENSION = "VK_KHR_surface";

			VkInstanceCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			createInfo.enabledExtensionCount = 1u;
			createInfo.ppEnabledExtensionNames = &EXTENSION;

			VkInstance hInstance = VK_NULL_HANDLE;

			if (pVkCreateInstance(&createInfo, nullptr, &hInstance) != VkResult::VK_SUCCESS) return VK_NULL_HANDLE;

			return hInstance;
		}


		static VkPhysicalDevice getPhysicalDevice(HMODULE hVulkan, VkInstance hInstance) {
			const PFN_vkEnumeratePhysicalDevices pVkEnumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(hax::proc::in::getProcAddress(hVulkan, "vkEnumeratePhysicalDevices"));
			const PFN_vkGetPhysicalDeviceProperties pVkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(hax::proc::in::getProcAddress(hVulkan, "vkGetPhysicalDeviceProperties"));
			
			if (!pVkEnumeratePhysicalDevices || !pVkGetPhysicalDeviceProperties) return VK_NULL_HANDLE;
			
			uint32_t gpuCount = 0u;

			if (pVkEnumeratePhysicalDevices(hInstance, &gpuCount, nullptr) != VkResult::VK_SUCCESS) return VK_NULL_HANDLE;

			if (!gpuCount) return VK_NULL_HANDLE;

			const uint32_t bufferSize = gpuCount * sizeof(VkPhysicalDevice);
			VkPhysicalDevice* const pPhysicalDevices = new VkPhysicalDevice[bufferSize]{};

			if (pVkEnumeratePhysicalDevices(hInstance, &gpuCount, pPhysicalDevices) != VkResult::VK_SUCCESS) return VK_NULL_HANDLE;

			VkPhysicalDevice hPhysicalDevice = VK_NULL_HANDLE;

			for (uint32_t i = 0u; i < bufferSize; i++) {
				VkPhysicalDeviceProperties properties{};
				pVkGetPhysicalDeviceProperties(pPhysicalDevices[i], &properties);

				if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
					hPhysicalDevice = pPhysicalDevices[i];
					break;
				}
			}

			delete[] pPhysicalDevices;

			return hPhysicalDevice;
		}


		static uint32_t getQueueFamily(HMODULE hVulkan, VkPhysicalDevice hPhysicalDevice);

		static VkDevice createDummyDevice(HMODULE hVulkan, VkPhysicalDevice hPhysicalDevice) {
			const uint32_t queueFamily = getQueueFamily(hVulkan, hPhysicalDevice);

			if (queueFamily == 0xFFFFFFFF) return VK_NULL_HANDLE;

			constexpr const char* EXTENSION = "VK_KHR_swapchain";
			constexpr float QUEUE_PRIORITY = 1.f;

			VkDeviceQueueCreateInfo queueInfo = {};
			queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfo.queueFamilyIndex = queueFamily;
			queueInfo.queueCount = 1;
			queueInfo.pQueuePriorities = &QUEUE_PRIORITY;

			VkDeviceCreateInfo createInfo0 = {};
			createInfo0.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
			createInfo0.queueCreateInfoCount = 1u;
			createInfo0.pQueueCreateInfos = &queueInfo;
			createInfo0.enabledExtensionCount = 1u;
			createInfo0.ppEnabledExtensionNames = &EXTENSION;

			const PFN_vkCreateDevice pVkCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(proc::in::getProcAddress(hVulkan, "vkCreateDevice"));

			if (!pVkCreateDevice) return VK_NULL_HANDLE;

			VkDevice hDummyDevice = VK_NULL_HANDLE;

			if (pVkCreateDevice(hPhysicalDevice, &createInfo0, nullptr, &hDummyDevice) != VkResult::VK_SUCCESS) return VK_NULL_HANDLE;

			return hDummyDevice;
		}


		static uint32_t getQueueFamily(HMODULE hVulkan, VkPhysicalDevice hPhysicalDevice) {
			const PFN_vkGetPhysicalDeviceQueueFamilyProperties pVkGetPhysicalDeviceQueueFamilyProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(proc::in::getProcAddress(hVulkan, "vkGetPhysicalDeviceQueueFamilyProperties"));

			if (!pVkGetPhysicalDeviceQueueFamilyProperties) return 0xFFFFFFFF;
			
			uint32_t propertiesCount = 0u;
			pVkGetPhysicalDeviceQueueFamilyProperties(hPhysicalDevice, &propertiesCount, nullptr);

			if (!propertiesCount) return 0xFFFFFFFF;

			const uint32_t bufferSize = propertiesCount;
			VkQueueFamilyProperties* const pProperties = new VkQueueFamilyProperties[bufferSize]{};

			pVkGetPhysicalDeviceQueueFamilyProperties(hPhysicalDevice, &propertiesCount, pProperties);

			uint32_t queueFamily = 0xFFFFFFFF;

			for (uint32_t i = 0u; i < bufferSize; i++) {

				if (pProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
					queueFamily = i;
					break;
				}

			}

			delete[] pProperties;

			return queueFamily;
		}


		Draw::Draw() : _pVkGetSwapchainImagesKHR{}, _pVkCreateCommandPool{}, _pVkDestroyCommandPool{},
			_pVkAllocateCommandBuffers{}, _pVkFreeCommandBuffers{}, _pVkCreateImageView{}, _pVkDestroyImageView{},
			_pVkCreateFramebuffer{}, _pVkDestroyFramebuffer{}, _pVkCreateRenderPass{}, _pVkDestroyRenderPass{},
			_pVkResetCommandBuffer{}, _pVkBeginCommandBuffer{}, _pVkCmdBeginRenderPass{}, _pVkCreateShaderModule{},
			_pVkDestroyShaderModule{},
			_hPhysicalDevice{}, _queueFamily{}, _hDevice{}, _hRenderPass{}, _hShaderModuleVert{}, _hShaderModuleFrag{},
			_hDescriptorSetLayout{}, _hPipelineLayout {}, _hPipeline{},
			_pImageData{}, _imageCount{},
			_isInit{} {}


		Draw::~Draw() {
			destroyImageData();

			if (this->_pVkDestroyRenderPass && this->_hDevice && this->_hRenderPass != VK_NULL_HANDLE) {
				this->_pVkDestroyRenderPass(this->_hDevice, this->_hRenderPass, nullptr);
			}

			if (this->_pVkDestroyShaderModule && this->_hDevice && this->_hShaderModuleVert != VK_NULL_HANDLE) {
				this->_pVkDestroyShaderModule(this->_hDevice, this->_hShaderModuleVert, nullptr);
			}

			if (this->_pVkDestroyShaderModule && this->_hDevice && this->_hShaderModuleFrag != VK_NULL_HANDLE) {
				this->_pVkDestroyShaderModule(this->_hDevice, this->_hShaderModuleFrag, nullptr);
			}
			
		}


		void Draw::beginDraw(Engine* pEngine) {

			if (!this->_isInit) {
				const HMODULE hVulkan = proc::in::getModuleHandle("vulkan-1.dll");

				if (!hVulkan) return;

				const PFN_vkDestroyInstance pVkDestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(proc::in::getProcAddress(hVulkan, "vkDestroyInstance"));

				if (!pVkDestroyInstance) return;

				const VkInstance hInstance = createInstance(hVulkan);

				if (hInstance == VK_NULL_HANDLE) return;

				if (!this->getProcAddresses(hVulkan, hInstance)) {
					pVkDestroyInstance(hInstance, nullptr);

					return;
				}

				this->_hPhysicalDevice = getPhysicalDevice(hVulkan, hInstance);
				pVkDestroyInstance(hInstance, nullptr);

				if (this->_hPhysicalDevice == VK_NULL_HANDLE) return;

				this->_queueFamily = getQueueFamily(hVulkan, this->_hPhysicalDevice);

				if (this->_queueFamily == 0xFFFFFFFF) return;

				this->_isInit = true;
			}

			const VkPresentInfoKHR* const pPresentInfo = reinterpret_cast<const VkPresentInfoKHR*>(pEngine->pHookArg2);
			this->_hDevice = reinterpret_cast<VkDevice>(pEngine->pHookArg3);

			if (!pPresentInfo || !this->_hDevice) return;

			for (uint32_t i = 0u; i < pPresentInfo->swapchainCount; i++) {
				
				if (!this->createRenderPass()) return;

				if (!this->createImageData(pPresentInfo->pSwapchains[i])) return;

				const ImageData curImageData = this->_pImageData[pPresentInfo->pImageIndices[i]];

				if (curImageData.hCommandBuffer == VK_NULL_HANDLE) return;
				
				if (this->_pVkResetCommandBuffer(curImageData.hCommandBuffer, 0) != VkResult::VK_SUCCESS) return;

				VkCommandBufferBeginInfo cmdBufferBeginInfo = {};
				cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				cmdBufferBeginInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

				if (this->_pVkBeginCommandBuffer(curImageData.hCommandBuffer, &cmdBufferBeginInfo) != VkResult::VK_SUCCESS) return;

				VkRenderPassBeginInfo renderPassBeginInfo = {};
				renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				renderPassBeginInfo.renderPass = this->_hRenderPass;
				renderPassBeginInfo.framebuffer = curImageData.hFrameBuffer;
				renderPassBeginInfo.renderArea.extent.width = 1366;
				renderPassBeginInfo.renderArea.extent.height = 768;

				this->_pVkCmdBeginRenderPass(curImageData.hCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				if (!this->createPipeline()) return;
			}

			return;
		}


		void Draw::endDraw(const Engine* pEngine) {
			
			return;
		}


		void Draw::drawString(const void* pFont, const Vector2* pos, const char* text, rgb::Color color) {


			return;
		}


		void Draw::drawTriangleList(const Vector2 corners[], UINT count, rgb::Color color) {

			return;
		}


		bool Draw::getProcAddresses(HMODULE hVulkan, VkInstance hInstance) {
			const PFN_vkGetInstanceProcAddr pVkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(proc::in::getProcAddress(hVulkan, "vkGetInstanceProcAddr"));
			
			if (!pVkGetInstanceProcAddr) return false;

			this->_pVkGetSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(pVkGetInstanceProcAddr(hInstance, "vkGetSwapchainImagesKHR"));
			this->_pVkCreateCommandPool = reinterpret_cast<PFN_vkCreateCommandPool>(pVkGetInstanceProcAddr(hInstance, "vkCreateCommandPool"));
			this->_pVkDestroyCommandPool = reinterpret_cast<PFN_vkDestroyCommandPool>(pVkGetInstanceProcAddr(hInstance, "vkDestroyCommandPool"));
			this->_pVkAllocateCommandBuffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(pVkGetInstanceProcAddr(hInstance, "vkAllocateCommandBuffers"));
			this->_pVkFreeCommandBuffers = reinterpret_cast<PFN_vkFreeCommandBuffers>(pVkGetInstanceProcAddr(hInstance, "vkFreeCommandBuffers"));
			this->_pVkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(pVkGetInstanceProcAddr(hInstance, "vkCreateImageView"));
			this->_pVkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(pVkGetInstanceProcAddr(hInstance, "vkDestroyImageView"));
			this->_pVkCreateFramebuffer = reinterpret_cast<PFN_vkCreateFramebuffer>(pVkGetInstanceProcAddr(hInstance, "vkCreateFramebuffer"));
			this->_pVkDestroyFramebuffer = reinterpret_cast<PFN_vkDestroyFramebuffer>(pVkGetInstanceProcAddr(hInstance, "vkDestroyFramebuffer"));
			this->_pVkCreateRenderPass = reinterpret_cast<PFN_vkCreateRenderPass>(pVkGetInstanceProcAddr(hInstance, "vkCreateRenderPass"));
			this->_pVkDestroyRenderPass = reinterpret_cast<PFN_vkDestroyRenderPass>(pVkGetInstanceProcAddr(hInstance, "vkDestroyRenderPass"));
			this->_pVkResetCommandBuffer = reinterpret_cast<PFN_vkResetCommandBuffer>(pVkGetInstanceProcAddr(hInstance, "vkResetCommandBuffer"));
			this->_pVkBeginCommandBuffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(pVkGetInstanceProcAddr(hInstance, "vkBeginCommandBuffer"));
			this->_pVkCmdBeginRenderPass = reinterpret_cast<PFN_vkCmdBeginRenderPass>(pVkGetInstanceProcAddr(hInstance, "vkCmdBeginRenderPass"));
			this->_pVkCreateShaderModule = reinterpret_cast<PFN_vkCreateShaderModule>(pVkGetInstanceProcAddr(hInstance, "vkCreateShaderModule"));
			this->_pVkDestroyShaderModule = reinterpret_cast<PFN_vkDestroyShaderModule>(pVkGetInstanceProcAddr(hInstance, "vkDestroyShaderModule"));
			this->_pVkCreatePipelineLayout = reinterpret_cast<PFN_vkCreatePipelineLayout>(pVkGetInstanceProcAddr(hInstance, "vkCreatePipelineLayout"));
			this->_pVkDestroyPipelineLayout = reinterpret_cast<PFN_vkDestroyPipelineLayout>(pVkGetInstanceProcAddr(hInstance, "vkDestroyPipelineLayout"));
			this->_pVkCreateGraphicsPipelines = reinterpret_cast<PFN_vkCreateGraphicsPipelines>(pVkGetInstanceProcAddr(hInstance, "vkCreateGraphicsPipelines"));
			this->_pVkDestroyPipeline = reinterpret_cast<PFN_vkDestroyPipeline>(pVkGetInstanceProcAddr(hInstance, "vkDestroyPipeline"));

			if (
				!this->_pVkGetSwapchainImagesKHR || !this->_pVkCreateCommandPool || !this->_pVkDestroyCommandPool ||
				!this->_pVkAllocateCommandBuffers || !this->_pVkFreeCommandBuffers || !this->_pVkCreateRenderPass ||
				!this->_pVkDestroyRenderPass || !this->_pVkCreateImageView || !this->_pVkDestroyImageView ||
				!this->_pVkCreateFramebuffer || !this->_pVkDestroyFramebuffer || !this->_pVkResetCommandBuffer ||
				!this->_pVkBeginCommandBuffer || !this->_pVkCmdBeginRenderPass || !this->_pVkCreateShaderModule ||
				!this->_pVkDestroyShaderModule || !this->_pVkCreatePipelineLayout || !this->_pVkDestroyPipelineLayout ||
				!this->_pVkCreateGraphicsPipelines || !this->_pVkDestroyPipeline
			) return false;

			return true;
		}


		bool Draw::createRenderPass() {
			VkAttachmentDescription attachment{};
			attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
			attachment.samples = VK_SAMPLE_COUNT_1_BIT;
			attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

			VkAttachmentReference colorAttachment{};
			colorAttachment.attachment = 0u;
			colorAttachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1u;
			subpass.pColorAttachments = &colorAttachment;

			VkRenderPassCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			info.attachmentCount = 1u;
			info.pAttachments = &attachment;
			info.subpassCount = 1u;
			info.pSubpasses = &subpass;

			return this->_pVkCreateRenderPass(this->_hDevice, &info, nullptr, &this->_hRenderPass) == VkResult::VK_SUCCESS;
		}


		bool Draw::createImageData(VkSwapchainKHR hSwapchain) {
			uint32_t imageCount = 0u;

			if (this->_pVkGetSwapchainImagesKHR(this->_hDevice, hSwapchain, &imageCount, nullptr) != VkResult::VK_SUCCESS) return false;

			if (!imageCount) return false;

			if (imageCount != this->_imageCount) {
				destroyImageData();

				this->_imageCount = imageCount;
			}

			if (!this->_pImageData) {
				this->_pImageData = new ImageData[this->_imageCount]{};
			}

			VkImage* const pImages = new VkImage[this->_imageCount]{};

			if (this->_pVkGetSwapchainImagesKHR(this->_hDevice, hSwapchain, &imageCount, pImages) != VkResult::VK_SUCCESS) {
				delete[] pImages;

				return false;
			}

			VkImageSubresourceRange imageRange{};
			imageRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageRange.baseMipLevel = 0u;
			imageRange.levelCount = 1u;
			imageRange.baseArrayLayer = 0u;
			imageRange.layerCount = 1u;

			VkImageViewCreateInfo imageViewInfo{};
			imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			imageViewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
			imageViewInfo.subresourceRange = imageRange;

			VkFramebufferCreateInfo frameBufferInfo = {};
			frameBufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			frameBufferInfo.renderPass = this->_hRenderPass;
			frameBufferInfo.attachmentCount = 1;
			frameBufferInfo.layers = 1;

			for (uint32_t i = 0; i < this->_imageCount; ++i) {
				VkCommandPoolCreateInfo createInfo{};
				createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
				createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
				createInfo.queueFamilyIndex = this->_queueFamily;

				if (this->_pVkCreateCommandPool(this->_hDevice, &createInfo, nullptr, &this->_pImageData[i].hCommandPool) != VkResult::VK_SUCCESS) continue;

				VkCommandBufferAllocateInfo allocInfo{};
				allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				allocInfo.commandPool = this->_pImageData[i].hCommandPool;
				allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				allocInfo.commandBufferCount = 1;

				if (this->_pVkAllocateCommandBuffers(this->_hDevice, &allocInfo, &this->_pImageData[i].hCommandBuffer) != VkResult::VK_SUCCESS) continue;

				imageViewInfo.image = pImages[i];
				
				if (this->_pVkCreateImageView(this->_hDevice, &imageViewInfo, nullptr, &this->_pImageData[i].hImageView) != VkResult::VK_SUCCESS) continue;

				frameBufferInfo.pAttachments = &this->_pImageData[i].hImageView;
				this->_pVkCreateFramebuffer(this->_hDevice, &frameBufferInfo, nullptr, &this->_pImageData[i].hFrameBuffer);
			}

			delete[] pImages;

			return true;
		}


		bool Draw::createPipeline() {

			if (!this->createShaderModules()) return false;

			if (!this->createPipelineLayout()) return false;

			VkPipelineShaderStageCreateInfo stageCreateInfo[2]{};
			stageCreateInfo[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageCreateInfo[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			stageCreateInfo[0].module = this->_hShaderModuleVert;
			stageCreateInfo[0].pName = "main";
			stageCreateInfo[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageCreateInfo[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stageCreateInfo[1].module = this->_hShaderModuleFrag;
			stageCreateInfo[1].pName = "main";

			VkVertexInputBindingDescription bindingDesc{};
			bindingDesc.stride = sizeof(Vertex);
			bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attributeDesc[2]{};
			attributeDesc[0].location = 0u;
			attributeDesc[0].binding = bindingDesc.binding;
			attributeDesc[0].format = VK_FORMAT_R32G32_SFLOAT;
			attributeDesc[0].offset = 0;
			attributeDesc[1].location = 1;
			attributeDesc[1].binding = bindingDesc.binding;
			attributeDesc[1].format = VK_FORMAT_R8G8B8A8_UNORM;
			attributeDesc[1].offset = sizeof(Vector2);

			VkPipelineVertexInputStateCreateInfo vertexInfo{};
			vertexInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInfo.vertexBindingDescriptionCount = 1;
			vertexInfo.pVertexBindingDescriptions = &bindingDesc;
			vertexInfo.vertexAttributeDescriptionCount = 2;
			vertexInfo.pVertexAttributeDescriptions = attributeDesc;

			VkPipelineInputAssemblyStateCreateInfo iaInfo{};
			iaInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			iaInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			VkPipelineViewportStateCreateInfo viewportInfo{};
			viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportInfo.viewportCount = 1;
			viewportInfo.scissorCount = 1;

			VkPipelineRasterizationStateCreateInfo rasterInfo{};
			rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
			rasterInfo.cullMode = VK_CULL_MODE_NONE;
			rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterInfo.lineWidth = 1.f;

			VkPipelineMultisampleStateCreateInfo multisampleInfo{};
			multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineColorBlendAttachmentState colorAttachment{};
			colorAttachment.blendEnable = VK_TRUE;
			colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
			colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			VkPipelineDepthStencilStateCreateInfo depthInfo{};
			depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

			VkPipelineColorBlendStateCreateInfo blendInfo{};
			blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blendInfo.attachmentCount = 1;
			blendInfo.pAttachments = &colorAttachment;

			VkDynamicState dynamicStates[2]{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = sizeof(dynamicStates) / sizeof(VkDynamicState);
			dynamicState.pDynamicStates = dynamicStates;

			VkGraphicsPipelineCreateInfo pipelineCreateInfo{};
			pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineCreateInfo.flags = 0;
			pipelineCreateInfo.stageCount = 2;
			pipelineCreateInfo.pStages = stageCreateInfo;
			pipelineCreateInfo.pVertexInputState = &vertexInfo;
			pipelineCreateInfo.pInputAssemblyState = &iaInfo;
			pipelineCreateInfo.pViewportState = &viewportInfo;
			pipelineCreateInfo.pRasterizationState = &rasterInfo;
			pipelineCreateInfo.pMultisampleState = &multisampleInfo;
			pipelineCreateInfo.pDepthStencilState = &depthInfo;
			pipelineCreateInfo.pColorBlendState = &blendInfo;
			pipelineCreateInfo.pDynamicState = &dynamicState;
			pipelineCreateInfo.layout = this->_hPipelineLayout;
			pipelineCreateInfo.renderPass = this->_hRenderPass;
			pipelineCreateInfo.subpass = 0;

			if (this->_pVkCreateGraphicsPipelines(this->_hDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &this->_hPipeline) != VkResult::VK_SUCCESS) return false;

			return true;
		}


		// glsl_shader.vert, compiled with:
		// # glslangValidator -V -x -o glsl_shader.vert.u32 glsl_shader.vert
		/*
		#version 450 core
		layout(location = 0) in vec2 aPos;
		layout(location = 1) in vec2 aUV;
		layout(location = 2) in vec4 aColor;
		layout(push_constant) uniform uPushConstant { vec2 uScale; vec2 uTranslate; } pc;

		out gl_PerVertex { vec4 gl_Position; };
		layout(location = 0) out struct { vec4 Color; vec2 UV; } Out;

		void main()
		{
			Out.Color = aColor;
			Out.UV = aUV;
			gl_Position = vec4(aPos * pc.uScale + pc.uTranslate, 0, 1);
		}
		*/

		static constexpr uint32_t GLSL_SHADER_VERT[] =
		{
			0x07230203,0x00010000,0x00080001,0x0000002e,0x00000000,0x00020011,0x00000001,0x0006000b,
			0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
			0x000a000f,0x00000000,0x00000004,0x6e69616d,0x00000000,0x0000000b,0x0000000f,0x00000015,
			0x0000001b,0x0000001c,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,
			0x00000000,0x00030005,0x00000009,0x00000000,0x00050006,0x00000009,0x00000000,0x6f6c6f43,
			0x00000072,0x00040006,0x00000009,0x00000001,0x00005655,0x00030005,0x0000000b,0x0074754f,
			0x00040005,0x0000000f,0x6c6f4361,0x0000726f,0x00030005,0x00000015,0x00565561,0x00060005,
			0x00000019,0x505f6c67,0x65567265,0x78657472,0x00000000,0x00060006,0x00000019,0x00000000,
			0x505f6c67,0x7469736f,0x006e6f69,0x00030005,0x0000001b,0x00000000,0x00040005,0x0000001c,
			0x736f5061,0x00000000,0x00060005,0x0000001e,0x73755075,0x6e6f4368,0x6e617473,0x00000074,
			0x00050006,0x0000001e,0x00000000,0x61635375,0x0000656c,0x00060006,0x0000001e,0x00000001,
			0x61725475,0x616c736e,0x00006574,0x00030005,0x00000020,0x00006370,0x00040047,0x0000000b,
			0x0000001e,0x00000000,0x00040047,0x0000000f,0x0000001e,0x00000002,0x00040047,0x00000015,
			0x0000001e,0x00000001,0x00050048,0x00000019,0x00000000,0x0000000b,0x00000000,0x00030047,
			0x00000019,0x00000002,0x00040047,0x0000001c,0x0000001e,0x00000000,0x00050048,0x0000001e,
			0x00000000,0x00000023,0x00000000,0x00050048,0x0000001e,0x00000001,0x00000023,0x00000008,
			0x00030047,0x0000001e,0x00000002,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,
			0x00030016,0x00000006,0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040017,
			0x00000008,0x00000006,0x00000002,0x0004001e,0x00000009,0x00000007,0x00000008,0x00040020,
			0x0000000a,0x00000003,0x00000009,0x0004003b,0x0000000a,0x0000000b,0x00000003,0x00040015,
			0x0000000c,0x00000020,0x00000001,0x0004002b,0x0000000c,0x0000000d,0x00000000,0x00040020,
			0x0000000e,0x00000001,0x00000007,0x0004003b,0x0000000e,0x0000000f,0x00000001,0x00040020,
			0x00000011,0x00000003,0x00000007,0x0004002b,0x0000000c,0x00000013,0x00000001,0x00040020,
			0x00000014,0x00000001,0x00000008,0x0004003b,0x00000014,0x00000015,0x00000001,0x00040020,
			0x00000017,0x00000003,0x00000008,0x0003001e,0x00000019,0x00000007,0x00040020,0x0000001a,
			0x00000003,0x00000019,0x0004003b,0x0000001a,0x0000001b,0x00000003,0x0004003b,0x00000014,
			0x0000001c,0x00000001,0x0004001e,0x0000001e,0x00000008,0x00000008,0x00040020,0x0000001f,
			0x00000009,0x0000001e,0x0004003b,0x0000001f,0x00000020,0x00000009,0x00040020,0x00000021,
			0x00000009,0x00000008,0x0004002b,0x00000006,0x00000028,0x00000000,0x0004002b,0x00000006,
			0x00000029,0x3f800000,0x00050036,0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,
			0x00000005,0x0004003d,0x00000007,0x00000010,0x0000000f,0x00050041,0x00000011,0x00000012,
			0x0000000b,0x0000000d,0x0003003e,0x00000012,0x00000010,0x0004003d,0x00000008,0x00000016,
			0x00000015,0x00050041,0x00000017,0x00000018,0x0000000b,0x00000013,0x0003003e,0x00000018,
			0x00000016,0x0004003d,0x00000008,0x0000001d,0x0000001c,0x00050041,0x00000021,0x00000022,
			0x00000020,0x0000000d,0x0004003d,0x00000008,0x00000023,0x00000022,0x00050085,0x00000008,
			0x00000024,0x0000001d,0x00000023,0x00050041,0x00000021,0x00000025,0x00000020,0x00000013,
			0x0004003d,0x00000008,0x00000026,0x00000025,0x00050081,0x00000008,0x00000027,0x00000024,
			0x00000026,0x00050051,0x00000006,0x0000002a,0x00000027,0x00000000,0x00050051,0x00000006,
			0x0000002b,0x00000027,0x00000001,0x00070050,0x00000007,0x0000002c,0x0000002a,0x0000002b,
			0x00000028,0x00000029,0x00050041,0x00000011,0x0000002d,0x0000001b,0x0000000d,0x0003003e,
			0x0000002d,0x0000002c,0x000100fd,0x00010038
		};

		// glsl_shader.frag, compiled with:
		// # glslangValidator -V -x -o glsl_shader.frag.u32 glsl_shader.frag
		/*
		#version 450 core
		layout(location = 0) out vec4 fColor;
		layout(set=0, binding=0) uniform sampler2D sTexture;
		layout(location = 0) in struct { vec4 Color; vec2 UV; } In;
		void main()
		{
			fColor = In.Color * texture(sTexture, In.UV.st);
		}
		*/
		static constexpr uint32_t GLSL_SHADER_FRAG[] =
		{
			0x07230203,0x00010000,0x00080001,0x0000001e,0x00000000,0x00020011,0x00000001,0x0006000b,
			0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
			0x0007000f,0x00000004,0x00000004,0x6e69616d,0x00000000,0x00000009,0x0000000d,0x00030010,
			0x00000004,0x00000007,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,
			0x00000000,0x00040005,0x00000009,0x6c6f4366,0x0000726f,0x00030005,0x0000000b,0x00000000,
			0x00050006,0x0000000b,0x00000000,0x6f6c6f43,0x00000072,0x00040006,0x0000000b,0x00000001,
			0x00005655,0x00030005,0x0000000d,0x00006e49,0x00050005,0x00000016,0x78655473,0x65727574,
			0x00000000,0x00040047,0x00000009,0x0000001e,0x00000000,0x00040047,0x0000000d,0x0000001e,
			0x00000000,0x00040047,0x00000016,0x00000022,0x00000000,0x00040047,0x00000016,0x00000021,
			0x00000000,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,0x00030016,0x00000006,
			0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040020,0x00000008,0x00000003,
			0x00000007,0x0004003b,0x00000008,0x00000009,0x00000003,0x00040017,0x0000000a,0x00000006,
			0x00000002,0x0004001e,0x0000000b,0x00000007,0x0000000a,0x00040020,0x0000000c,0x00000001,
			0x0000000b,0x0004003b,0x0000000c,0x0000000d,0x00000001,0x00040015,0x0000000e,0x00000020,
			0x00000001,0x0004002b,0x0000000e,0x0000000f,0x00000000,0x00040020,0x00000010,0x00000001,
			0x00000007,0x00090019,0x00000013,0x00000006,0x00000001,0x00000000,0x00000000,0x00000000,
			0x00000001,0x00000000,0x0003001b,0x00000014,0x00000013,0x00040020,0x00000015,0x00000000,
			0x00000014,0x0004003b,0x00000015,0x00000016,0x00000000,0x0004002b,0x0000000e,0x00000018,
			0x00000001,0x00040020,0x00000019,0x00000001,0x0000000a,0x00050036,0x00000002,0x00000004,
			0x00000000,0x00000003,0x000200f8,0x00000005,0x00050041,0x00000010,0x00000011,0x0000000d,
			0x0000000f,0x0004003d,0x00000007,0x00000012,0x00000011,0x0004003d,0x00000014,0x00000017,
			0x00000016,0x00050041,0x00000019,0x0000001a,0x0000000d,0x00000018,0x0004003d,0x0000000a,
			0x0000001b,0x0000001a,0x00050057,0x00000007,0x0000001c,0x00000017,0x0000001b,0x00050085,
			0x00000007,0x0000001d,0x00000012,0x0000001c,0x0003003e,0x00000009,0x0000001d,0x000100fd,
			0x00010038
		};

		bool Draw::createShaderModules() {

			if (this->_hShaderModuleVert == VK_NULL_HANDLE)
			{
				VkShaderModuleCreateInfo vertCreateInfo{};
				vertCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				vertCreateInfo.codeSize = sizeof(GLSL_SHADER_VERT);
				vertCreateInfo.pCode = GLSL_SHADER_VERT;
				
				if (this->_pVkCreateShaderModule(this->_hDevice, &vertCreateInfo, nullptr, &this->_hShaderModuleVert) != VkResult::VK_SUCCESS) return false;
			}

			if (this->_hShaderModuleFrag == VK_NULL_HANDLE)
			{
				VkShaderModuleCreateInfo fragCreateInfo{};
				fragCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				fragCreateInfo.codeSize = sizeof(GLSL_SHADER_FRAG);
				fragCreateInfo.pCode = GLSL_SHADER_FRAG;
				
				if (this->_pVkCreateShaderModule(this->_hDevice, &fragCreateInfo, nullptr, &this->_hShaderModuleFrag) != VkResult::VK_SUCCESS) return false;
			
			}

			return true;
		}


		bool Draw::createPipelineLayout()
		{
			
			if (this->_hDescriptorSetLayout != VK_NULL_HANDLE)
			{
				VkDescriptorSetLayoutBinding binding{};
				binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				binding.descriptorCount = 1;
				binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

				VkDescriptorSetLayoutCreateInfo descCreateinfo{};
				descCreateinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				descCreateinfo.bindingCount = 1;
				descCreateinfo.pBindings = &binding;
			}

			if (this->_hPipelineLayout != VK_NULL_HANDLE)
			{
				VkPushConstantRange pushConstants{};
				pushConstants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
				pushConstants.offset = sizeof(float) * 0;
				pushConstants.size = sizeof(float) * 4;
				
				VkDescriptorSetLayout setLayout = this->_hDescriptorSetLayout;
				
				VkPipelineLayoutCreateInfo layoutCreateInfo{};
				layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				layoutCreateInfo.setLayoutCount = 1;
				layoutCreateInfo.pSetLayouts = &setLayout;
				layoutCreateInfo.pushConstantRangeCount = 1;
				layoutCreateInfo.pPushConstantRanges = &pushConstants;
				
				if (!this->_pVkCreatePipelineLayout(this->_hDevice, &layoutCreateInfo, nullptr, &this->_hPipelineLayout) != VkResult::VK_SUCCESS) return false;

			}
			
			return true;
		}


		void Draw::destroyImageData() {

			if (!this->_pVkFreeCommandBuffers || !this->_pVkDestroyCommandPool) return;

			if (this->_pImageData) {

				for (uint32_t i = 0u; i < this->_imageCount; i++) {

					if (this->_pImageData[i].hFrameBuffer != VK_NULL_HANDLE) {
						this->_pVkDestroyFramebuffer(this->_hDevice, this->_pImageData[i].hFrameBuffer, nullptr);
					}
					
					if (this->_pImageData[i].hImageView != VK_NULL_HANDLE) {
						this->_pVkDestroyImageView(this->_hDevice, this->_pImageData[i].hImageView, nullptr);
					}

					if (this->_pImageData[i].hCommandBuffer != VK_NULL_HANDLE) {
						this->_pVkFreeCommandBuffers(this->_hDevice, this->_pImageData[i].hCommandPool, 1, &this->_pImageData[i].hCommandBuffer);
					}
					
					if (this->_pImageData[i].hCommandPool != VK_NULL_HANDLE) {
						this->_pVkDestroyCommandPool(this->_hDevice, this->_pImageData[i].hCommandPool, nullptr);
					}

				}

				delete[] this->_pImageData;
				this->_pImageData = nullptr;
			}

			return;
		}

	}

}